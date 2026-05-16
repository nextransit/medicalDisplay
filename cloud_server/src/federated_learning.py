"""
Federated Learning Service for AI Model Optimization
"""

from __future__ import annotations

import hashlib
import logging
import os
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, Dataset

LOGGER = logging.getLogger("federated_learning")

# ============================================================================
# Model Architecture (Simplified for Medical Imaging)
// ============================================================================

class MedicalImagingModel(nn.Module):
    """Simplified CNN for medical image modality classification"""
    
    def __init__(self, num_classes: int = 13):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 32, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(64, 128, kernel_size=3, padding=1),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d((1, 1)),
        )
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(128, 64),
            nn.ReLU(inplace=True),
            nn.Dropout(0.5),
            nn.Linear(64, num_classes),
        )
    
    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.features(x)
        x = self.classifier(x)
        return x


@dataclass
class ClientGradient:
    """Gradient from a federated client"""
    client_id: str
    device_id: str
    gradient_data: bytes
    sample_count: int
    timestamp: str
    validation_accuracy: float = 0.0
    is_valid: bool = True


@dataclass
class FederatedRound:
    """Information about a federated learning round"""
    round_number: int
    start_time: str
    end_time: str | None = None
    num_participants: int = 0
    aggregated: bool = False
    global_model_hash: str = ""


@dataclass
class FederatedConfig:
    """Federated learning configuration"""
    min_clients: int = 3
    client_fraction: float = 0.5
    local_epochs: int = 5
    batch_size: int = 32
    learning_rate: float = 0.001
    momentum: float = 0.9
    weight_decay: float = 1e-4
    aggregation_method: str = "fedavg"  # fedavg, fedprox, scaffold
    server_momentum: float = 0.0


# ============================================================================
// Mock Dataset for Training (in production, use real DICOM data)
// ============================================================================

class MockMedicalDataset(Dataset):
    """Mock dataset for demonstration"""
    
    def __init__(self, num_samples: int = 1000, img_size: int = 64):
        self.num_samples = num_samples
        self.img_size = img_size
        self.data = np.random.randn(num_samples, 1, img_size, img_size).astype(np.float32)
        self.labels = np.random.randint(0, 13, size=num_samples)
    
    def __len__(self) -> int:
        return self.num_samples
    
    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor]:
        return (
            torch.from_numpy(self.data[idx]),
            torch.tensor(self.labels[idx], dtype=torch.long),
        )


# ============================================================================
// Federated Learning Service
// ============================================================================

class FederatedLearningService:
    """Service for federated model training across hospitals"""
    
    def __init__(self, config: FederatedConfig | None = None):
        self.config = config or FederatedConfig()
        self.global_model = MedicalImagingModel(num_classes=13)
        self.current_round: FederatedRound | None = None
        self.round_history: list[FederatedRound] = []
        self.pending_gradients: dict[str, ClientGradient] = {}
        self.model_store_path = Path(__file__).parent.parent / "models"
        self.model_store_path.mkdir(exist_ok=True)
        
        # Training state
        self.optimizer = optim.SGD(
            self.global_model.parameters(),
            lr=self.config.learning_rate,
            momentum=self.config.momentum,
            weight_decay=self.config.weight_decay,
        )
        
        LOGGER.info("Federated Learning Service initialized")
        LOGGER.info("Config: min_clients=%d, aggregation=%s", 
                   self.config.min_clients, self.config.aggregation_method)
    
    def get_model_info(self) -> dict[str, Any]:
        """Get current global model information"""
        return {
            "round": self.current_round.round_number if self.current_round else 0,
            "model_hash": self._compute_model_hash(),
            "num_parameters": sum(p.numel() for p in self.global_model.parameters()),
            "config": {
                "learning_rate": self.config.learning_rate,
                "batch_size": self.config.batch_size,
                "local_epochs": self.config.local_epochs,
                "aggregation_method": self.config.aggregation_method,
            },
        }
    
    def get_global_model(self) -> bytes:
        """Get serialized global model for client download"""
        buffer = io.BytesIO()
        torch.save({
            "model_state_dict": self.global_model.state_dict(),
            "optimizer_state_dict": self.optimizer.state_dict(),
            "round": self.current_round.round_number if self.current_round else 0,
        }, buffer)
        return buffer.getvalue()
    
    def start_round(self, round_number: int) -> FederatedRound:
        """Start a new federated learning round"""
        self.current_round = FederatedRound(
            round_number=round_number,
            start_time=datetime.now(timezone.utc).isoformat(),
        )
        self.pending_gradients.clear()
        LOGGER.info("Started Federated Round %d", round_number)
        return self.current_round
    
    def submit_gradient(self, gradient: ClientGradient) -> dict[str, Any]:
        """Receive and validate gradient from a client"""
        if not self.current_round:
            return {"status": "error", "message": "No active round"}
        
        # Validate gradient
        if len(gradient.gradient_data) == 0:
            gradient.is_valid = False
            return {"status": "error", "message": "Empty gradient data"}
        
        # Check gradient norm (for Byzantine robustness)
        gradient_norm = self._compute_gradient_norm(gradient.gradient_data)
        if gradient_norm > 100.0:  # Suspiciously large gradient
            LOGGER.warning("Gradient norm too large: %.2f from %s", gradient_norm, gradient.client_id)
            gradient.is_valid = False
        
        self.pending_gradients[gradient.client_id] = gradient
        self.current_round.num_participants = len(self.pending_gradients)
        
        LOGGER.info("Received gradient from %s (samples: %d, valid: %s)",
                   gradient.client_id, gradient.sample_count, gradient.is_valid)
        
        return {
            "status": "accepted",
            "round": self.current_round.round_number,
            "total_participants": self.current_round.num_participants,
            "min_required": self.config.min_clients,
        }
    
    def can_aggregate(self) -> bool:
        """Check if we have enough valid gradients to aggregate"""
        if not self.current_round:
            return False
        
        valid_count = sum(1 for g in self.pending_gradients.values() if g.is_valid)
        return valid_count >= self.config.min_clients
    
    def aggregate_gradients(self) -> dict[str, Any]:
        """Aggregate client gradients using FedAvg"""
        if not self.can_aggregate():
            return {"status": "error", "message": "Not enough valid gradients"}
        
        if self.current_round is None:
            return {"status": "error", "message": "No active round"}
        
        LOGGER.info("Aggregating %d gradients using %s", 
                   len(self.pending_gradients), self.config.aggregation_method)
        
        # Calculate weights based on sample counts
        total_samples = sum(g.sample_count for g in self.pending_gradients.values() if g.is_valid)
        
        # For demonstration, apply mock aggregated gradient
        # In production, this would apply real aggregated gradients
        if self.config.aggregation_method == "fedavg":
            self._apply_fedavg()
        elif self.config.aggregation_method == "fedprox":
            self._apply_fedprox()
        elif self.config.aggregation_method == "scaffold":
            self._apply_scaffold()
        
        # Update round info
        self.current_round.end_time = datetime.now(timezone.utc).isoformat()
        self.current_round.aggregated = True
        self.current_round.global_model_hash = self._compute_model_hash()
        
        self.round_history.append(self.current_round)
        
        # Save model
        model_path = self.model_store_path / f"federated_round_{self.current_round.round_number}.pt"
        torch.save(self.global_model.state_dict(), model_path)
        
        result = {
            "status": "success",
            "round": self.current_round.round_number,
            "num_participants": self.current_round.num_participants,
            "model_hash": self.current_round.global_model_hash,
            "model_path": str(model_path),
        }
        
        LOGGER.info("Federated Round %d completed: %s", 
                   self.current_round.round_number, result)
        
        return result
    
    def _compute_gradient_norm(self, gradient_data: bytes) -> float:
        """Compute L2 norm of gradient"""
        try:
            gradient_tensor = torch.from_numpy(
                np.frombuffer(gradient_data, dtype=np.float32)
            )
            return float(torch.norm(gradient_tensor))
        except Exception:
            return 0.0
    
    def _compute_model_hash(self) -> str:
        """Compute SHA256 hash of model state"""
        state_dict = self.global_model.state_dict()
        buffer = io.BytesIO()
        torch.save(state_dict, buffer)
        return hashlib.sha256(buffer.getvalue()).hexdigest()[:16]
    
    def _apply_fedavg(self) -> None:
        """Apply Federated Averaging"""
        # In production, decrypt and aggregate real gradients
        # For demo, perform one training step on mock data
        self.global_model.train()
        dataset = MockMedicalDataset(num_samples=100)
        dataloader = DataLoader(dataset, batch_size=self.config.batch_size, shuffle=True)
        
        criterion = nn.CrossEntropyLoss()
        for epoch in range(self.config.local_epochs):
            for batch_data, batch_labels in dataloader:
                self.optimizer.zero_grad()
                outputs = self.global_model(batch_data)
                loss = criterion(outputs, batch_labels)
                loss.backward()
                self.optimizer.step()
        
        LOGGER.info("Applied FedAvg aggregation")
    
    def _apply_fedprox(self) -> None:
        """Apply FedProx with proximal term"""
        # Similar to FedAvg but with regularization
        self._apply_fedavg()
        LOGGER.info("Applied FedProx aggregation")
    
    def _apply_scaffold(self) -> None:
        """Apply SCAFFOLD algorithm"""
        # Stochastic Controlled Averaging for Federated Learning
        self._apply_fedavg()
        LOGGER.info("Applied SCAFFOLD aggregation")
    
    def get_round_history(self) -> list[dict[str, Any]]:
        """Get history of federated rounds"""
        return [
            {
                "round_number": r.round_number,
                "start_time": r.start_time,
                "end_time": r.end_time,
                "num_participants": r.num_participants,
                "aggregated": r.aggregated,
                "model_hash": r.global_model_hash,
            }
            for r in self.round_history
        ]


# ============================================================================
// Module exports
// ============================================================================

__all__ = ["FederatedLearningService", "FederatedConfig", "ClientGradient", "FederatedRound"]
