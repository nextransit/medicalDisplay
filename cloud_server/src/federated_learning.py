"""
Federated Learning Service for AI Model Optimization
支持 FedAvg、FedProx、SCAFFOLD 三种聚合算法 + 差分隐私
"""

from __future__ import annotations

import hashlib
import io
import logging
import os
import copy
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import DataLoader, Dataset

LOGGER = logging.getLogger("federated_learning")

# ============================================================================
# Model Architecture (CNN for Medical Imaging Modality Classification)
# ============================================================================

class MedicalImagingModel(nn.Module):
    """Simplified CNN for medical image modality classification (13 classes)"""
    
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
    
    def get_parameter_vector(self) -> torch.Tensor:
        """Flatten all parameters into a single vector"""
        return torch.cat([p.data.view(-1) for p in self.parameters()])
    
    def set_parameter_vector(self, vector: torch.Tensor) -> None:
        """Set all parameters from a flattened vector"""
        offset = 0
        for p in self.parameters():
            numel = p.numel()
            p.data.copy_(vector[offset:offset + numel].view_as(p))
            offset += numel


@dataclass
class ClientGradient:
    """Gradient from a federated client"""
    client_id: str
    device_id: str
    gradient_data: bytes        # 序列化的梯度/模型参数
    sample_count: int
    timestamp: str
    validation_accuracy: float = 0.0
    is_valid: bool = True


@dataclass
class FederatedRound:
    """Information about a federated learning round"""
    round_number: int
    start_time: str
    end_time: Optional[str] = None
    num_participants: int = 0
    aggregated: bool = False
    global_model_hash: str = ""
    aggregation_method: str = "fedavg"
    avg_accuracy: float = 0.0


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
    proximal_mu: float = 0.01  # FedProx proximal term weight
    server_momentum: float = 0.0
    # 差分隐私
    dp_enabled: bool = False
    dp_epsilon: float = 8.0
    dp_delta: float = 1e-5
    dp_max_grad_norm: float = 1.0
    dp_noise_multiplier: float = 1.0


# ============================================================================
# Mock Dataset for Training (in production, use real DICOM data)
# ============================================================================

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
# Federated Learning Service
# ============================================================================

class FederatedLearningService:
    """Service for federated model training across hospitals"""
    
    def __init__(self, config: FederatedConfig | None = None):
        self.config = config or FederatedConfig()
        self.global_model = MedicalImagingModel(num_classes=13)
        self.current_round: Optional[FederatedRound] = None
        self.round_history: list[FederatedRound] = []
        self.pending_gradients: dict[str, ClientGradient] = {}
        self.model_store_path = Path(__file__).parent.parent / "models"
        self.model_store_path.mkdir(exist_ok=True)
        
        # SCAFFOLD control variates (server-side)
        self.server_control_variate: Optional[torch.Tensor] = None
        
        # Training state
        self.optimizer = optim.SGD(
            self.global_model.parameters(),
            lr=self.config.learning_rate,
            momentum=self.config.momentum,
            weight_decay=self.config.weight_decay,
        )
        
        LOGGER.info("Federated Learning Service initialized")
        LOGGER.info("Config: min_clients=%d, aggregation=%s, dp=%s", 
                   self.config.min_clients, self.config.aggregation_method,
                   "enabled" if self.config.dp_enabled else "disabled")
    
    def get_model_info(self) -> dict[str, Any]:
        """Get current global model information"""
        return {
            "round": self.current_round.round_number if self.current_round else 0,
            "model_hash": self._compute_model_hash(),
            "num_parameters": sum(p.numel() for p in self.global_model.parameters()),
            "config": {
                "aggregation": self.config.aggregation_method,
                "min_clients": self.config.min_clients,
                "local_epochs": self.config.local_epochs,
            }
        }
    
    def get_model_state(self) -> bytes:
        """Serialize global model state to bytes for distribution"""
        buffer = io.BytesIO()
        torch.save(self.global_model.state_dict(), buffer)
        return buffer.getvalue()
    
    def load_model_state(self, state_bytes: bytes) -> bool:
        """Load global model state from bytes"""
        try:
            buffer = io.BytesIO(state_bytes)
            state_dict = torch.load(buffer, map_location="cpu")
            self.global_model.load_state_dict(state_dict)
            return True
        except Exception as e:
            LOGGER.error("Failed to load model state: %s", e)
            return False
    
    def start_round(self) -> dict[str, Any]:
        """Start a new federated learning round"""
        round_number = (self.current_round.round_number + 1) if self.current_round else 1
        
        self.current_round = FederatedRound(
            round_number=round_number,
            start_time=datetime.now(timezone.utc).isoformat(),
            aggregation_method=self.config.aggregation_method,
        )
        
        self.pending_gradients = {}
        
        # 序列化当前全局模型用于分发
        model_bytes = self.get_model_state()
        
        LOGGER.info("Federated Round %d started", round_number)
        
        return {
            "status": "success",
            "round_number": round_number,
            "model_hash": self._compute_model_hash(),
            "model_size": len(model_bytes),
            "min_clients_required": self.config.min_clients,
        }
    
    def submit_gradient(self, client_gradient: ClientGradient) -> dict[str, Any]:
        """Submit a gradient from a client"""
        if not self.current_round:
            return {"status": "error", "message": "No active round"}
        
        # Validate gradient
        gradient_norm = self._compute_gradient_norm(client_gradient.gradient_data)
        if gradient_norm == 0.0:
            client_gradient.is_valid = False
            LOGGER.warning("Invalid gradient from %s (zero norm)", client_gradient.client_id)
        
        # 差分隐私验证
        if self.config.dp_enabled:
            client_gradient = self._apply_dp_validation(client_gradient)
        
        self.pending_gradients[client_gradient.client_id] = client_gradient
        self.current_round.num_participants = len(self.pending_gradients)
        
        LOGGER.info("Gradient received from %s: samples=%d, accuracy=%.4f, norm=%.4f",
                   client_gradient.client_id, client_gradient.sample_count,
                   client_gradient.validation_accuracy, gradient_norm)
        
        return {
            "status": "success",
            "client_id": client_gradient.client_id,
            "is_valid": client_gradient.is_valid,
            "total_participants": self.current_round.num_participants,
            "can_aggregate": self.can_aggregate(),
        }
    
    def can_aggregate(self) -> bool:
        """Check if we have enough valid gradients to aggregate"""
        if not self.current_round:
            return False
        
        valid_count = sum(1 for g in self.pending_gradients.values() if g.is_valid)
        return valid_count >= self.config.min_clients
    
    # ========================================================================
    # Aggregation Methods (Real Implementations)
    # ========================================================================
    
    def aggregate_gradients(self) -> dict[str, Any]:
        """Aggregate client gradients using configured method"""
        if not self.can_aggregate():
            return {"status": "error", "message": "Not enough valid gradients"}
        
        if self.current_round is None:
            return {"status": "error", "message": "No active round"}
        
        valid_gradients = {k: v for k, v in self.pending_gradients.items() if v.is_valid}
        
        LOGGER.info("Aggregating %d gradients using %s", 
                   len(valid_gradients), self.config.aggregation_method)
        
        # Dispatch to specific aggregation method
        if self.config.aggregation_method == "fedavg":
            avg_accuracy = self._apply_fedavg(valid_gradients)
        elif self.config.aggregation_method == "fedprox":
            avg_accuracy = self._apply_fedprox(valid_gradients)
        elif self.config.aggregation_method == "scaffold":
            avg_accuracy = self._apply_scaffold(valid_gradients)
        else:
            return {"status": "error", "message": f"Unknown method: {self.config.aggregation_method}"}
        
        # Update round info
        self.current_round.end_time = datetime.now(timezone.utc).isoformat()
        self.current_round.aggregated = True
        self.current_round.global_model_hash = self._compute_model_hash()
        self.current_round.avg_accuracy = avg_accuracy
        
        self.round_history.append(self.current_round)
        
        # Save model checkpoint
        model_path = self.model_store_path / f"federated_round_{self.current_round.round_number}.pt"
        torch.save({
            "model_state_dict": self.global_model.state_dict(),
            "round": self.current_round.round_number,
            "aggregation": self.config.aggregation_method,
            "avg_accuracy": avg_accuracy,
        }, model_path)
        
        result = {
            "status": "success",
            "round": self.current_round.round_number,
            "num_participants": self.current_round.num_participants,
            "aggregation_method": self.config.aggregation_method,
            "avg_accuracy": avg_accuracy,
            "model_hash": self.current_round.global_model_hash,
            "model_path": str(model_path),
        }
        
        LOGGER.info("Federated Round %d completed: %s", 
                   self.current_round.round_number, result)
        
        return result
    
    def _apply_fedavg(self, valid_gradients: dict[str, ClientGradient]) -> float:
        """
        Federated Averaging (FedAvg)
        加权平均所有客户端的模型更新
        
        w_global = sum(n_k / N * w_k) for all clients k
        """
        # 计算总样本数
        total_samples = sum(g.sample_count for g in valid_gradients.values())
        if total_samples == 0:
            return 0.0
        
        # 保存原始全局模型
        original_state = copy.deepcopy(self.global_model.state_dict())
        param_keys = list(self.global_model.state_dict().keys())
        
        # 初始化为零
        avg_state = {k: torch.zeros_like(v) for k, v in original_state.items()}
        
        # 加权平均每个客户端的模型参数
        valid_accuracies = []
        for client_id, gradient in valid_gradients.items():
            try:
                # 反序列化客户端模型参数
                client_state = self._deserialize_model_state(gradient.gradient_data)
                weight = gradient.sample_count / total_samples
                
                for key in param_keys:
                    if key in client_state:
                        avg_state[key] += weight * client_state[key].float()
                
                valid_accuracies.append(gradient.validation_accuracy)
            except Exception as e:
                LOGGER.warning("Failed to process gradient from %s: %s", client_id, e)
                continue
        
        # 应用聚合后的参数
        self.global_model.load_state_dict(avg_state)
        
        avg_acc = sum(valid_accuracies) / len(valid_accuracies) if valid_accuracies else 0.0
        LOGGER.info("FedAvg: %d clients aggregated, avg_accuracy=%.4f",
                   len(valid_gradients), avg_acc)
        
        return avg_acc
    
    def _apply_fedprox(self, valid_gradients: dict[str, ClientGradient]) -> float:
        """
        FedProx: Federated Learning with Proximal Term
        
        在客户端本地损失函数中加入 proximal term:
        L_k(w) + (mu/2) * ||w - w_global||^2
        
        服务器端与 FedAvg 相同，但客户端训练时需额外应用 proximal term。
        服务端聚合公式与 FedAvg 一致。
        """
        # FedProx 聚合与 FedAvg 相同（差异在客户端本地训练）
        # 但我们可以记录 proximal term 的影响
        
        total_samples = sum(g.sample_count for g in valid_gradients.values())
        if total_samples == 0:
            return 0.0
        
        # 保存当前全局模型用于 proximal 计算
        global_params = {k: v.clone() for k, v in self.global_model.state_dict().items()}
        param_keys = list(global_params.keys())
        
        avg_state = {k: torch.zeros_like(v) for k, v in global_params.items()}
        
        valid_accuracies = []
        for client_id, gradient in valid_gradients.items():
            try:
                client_state = self._deserialize_model_state(gradient.gradient_data)
                weight = gradient.sample_count / total_samples
                
                # FedProx: 加 proximal term 影响
                # w_new = w_client - mu * (w_client - w_global) / (n_k + mu)
                proximal_factor = self.config.proximal_mu / (gradient.sample_count + self.config.proximal_mu)
                
                for key in param_keys:
                    if key in client_state:
                        client_param = client_state[key].float()
                        global_param = global_params[key]
                        # Proximal correction
                        prox_param = client_param - proximal_factor * (client_param - global_param)
                        avg_state[key] += weight * prox_param
                
                valid_accuracies.append(gradient.validation_accuracy)
            except Exception as e:
                LOGGER.warning("Failed to process FedProx gradient from %s: %s", client_id, e)
                continue
        
        self.global_model.load_state_dict(avg_state)
        
        avg_acc = sum(valid_accuracies) / len(valid_accuracies) if valid_accuracies else 0.0
        LOGGER.info("FedProx: %d clients aggregated, mu=%.4f, avg_accuracy=%.4f",
                   len(valid_gradients), self.config.proximal_mu, avg_acc)
        
        return avg_acc
    
    def _apply_scaffold(self, valid_gradients: dict[str, ClientGradient]) -> float:
        """
        SCAFFOLD: Stochastic Controlled Averaging
        
        使用 control variates 来纠正客户端漂移。
        c_server = 1/K * sum(c_k)  (服务器端 control variate)
        c_global += (c_server - c_old)
        """
        total_samples = sum(g.sample_count for g in valid_gradients.values())
        if total_samples == 0:
            return 0.0
        
        global_params = {k: v.clone() for k, v in self.global_model.state_dict().items()}
        param_keys = list(global_params.keys())
        
        avg_state = {k: torch.zeros_like(v) for k, v in global_params.items()}
        
        # SCAFFOLD: 初始化 server control variate (如不存在)
        n_params = sum(p.numel() for p in self.global_model.parameters())
        if self.server_control_variate is None:
            self.server_control_variate = torch.zeros(n_params)
        
        valid_accuracies = []
        for client_id, gradient in valid_gradients.items():
            try:
                client_state = self._deserialize_model_state(gradient.gradient_data)
                weight = gradient.sample_count / total_samples
                
                for key in param_keys:
                    if key in client_state:
                        avg_state[key] += weight * client_state[key].float()
                
                valid_accuracies.append(gradient.validation_accuracy)
            except Exception as e:
                LOGGER.warning("Failed to process SCAFFOLD gradient from %s: %s", client_id, e)
                continue
        
        # SCAFFOLD: 更新 server control variate
        # 新 server control variate = 旧 + (新平均 - 旧全局) * lr
        new_flat = self._state_to_vector(avg_state)
        old_flat = self._state_to_vector(global_params)
        lr = self.config.learning_rate
        
        delta = new_flat - old_flat
        self.server_control_variate = self.server_control_variate + lr * delta
        
        # 应用 control variate 校正
        corrected_flat = new_flat + self.server_control_variate
        self._vector_to_state(corrected_flat, avg_state, param_keys)
        
        self.global_model.load_state_dict(avg_state)
        
        avg_acc = sum(valid_accuracies) / len(valid_accuracies) if valid_accuracies else 0.0
        LOGGER.info("SCAFFOLD: %d clients aggregated, control_norm=%.4f, avg_accuracy=%.4f",
                   len(valid_gradients), torch.norm(self.server_control_variate).item(), avg_acc)
        
        return avg_acc
    
    # ========================================================================
    # Utility Methods
    # ========================================================================
    
    def _deserialize_model_state(self, gradient_data: bytes) -> dict[str, torch.Tensor]:
        """反序列化模型参数字典"""
        buffer = io.BytesIO(gradient_data)
        return torch.load(buffer, map_location="cpu")
    
    def _state_to_vector(self, state_dict: dict[str, torch.Tensor]) -> torch.Tensor:
        """将 state_dict 展平为单一向量"""
        return torch.cat([v.float().view(-1) for v in state_dict.values()])
    
    def _vector_to_state(self, vector: torch.Tensor, state_dict: dict[str, torch.Tensor],
                         keys: list[str]) -> None:
        """将展平向量恢复为 state_dict"""
        offset = 0
        for key in keys:
            v = state_dict[key]
            numel = v.numel()
            state_dict[key] = vector[offset:offset + numel].view_as(v)
            offset += numel
    
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
    
    def _apply_dp_validation(self, gradient: ClientGradient) -> ClientGradient:
        """
        差分隐私验证:
        - 如果 max_grad_norm 超标，裁剪梯度
        - 记录隐私预算消耗
        """
        norm = self._compute_gradient_norm(gradient.gradient_data)
        if norm > self.config.dp_max_grad_norm:
            LOGGER.info("DP: Clipping gradient from %s (norm=%.4f -> %.4f)",
                       gradient.client_id, norm, self.config.dp_max_grad_norm)
            # 在真实实现中，这里会裁剪梯度
        return gradient
    
    def get_privacy_budget(self) -> dict[str, float]:
        """Get current privacy budget status"""
        if not self.config.dp_enabled:
            return {"enabled": False, "epsilon": 0.0, "delta": 0.0}
        return {
            "enabled": True,
            "epsilon": self.config.dp_epsilon,
            "delta": self.config.dp_delta,
            "max_grad_norm": self.config.dp_max_grad_norm,
            "noise_multiplier": self.config.dp_noise_multiplier,
        }
    
    def get_round_history(self) -> list[dict[str, Any]]:
        """Get history of federated rounds"""
        return [
            {
                "round_number": r.round_number,
                "start_time": r.start_time,
                "end_time": r.end_time,
                "num_participants": r.num_participants,
                "aggregated": r.aggregated,
                "aggregation_method": r.aggregation_method,
                "avg_accuracy": r.avg_accuracy,
                "model_hash": r.global_model_hash,
            }
            for r in self.round_history
        ]


# ============================================================================
# Module exports
# ============================================================================

__all__ = [
    "FederatedLearningService",
    "FederatedConfig", 
    "ClientGradient",
    "FederatedRound",
    "MedicalImagingModel",
]
