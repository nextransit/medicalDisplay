import QtQuick 2.15
import QtQuick.Controls 2.15

/**
 * 测量标注叠加层 — Canvas 绘制测量图形
 *
 * 支持:
 *   - 距离测量 (两点连线 + 像素/mm 标签)
 *   - 角度测量 (三点 + 角度弧线)
 *   - ROI 矩形 (拖拽绘制 + 面积显示)
 */

Item {
    id: root
    anchors.fill: parent

    property int toolMode: 0         // 0=无, 1=距离, 2=角度, 3=ROI
    property real pixelSpacing: 0.5  // mm/pixel (默认值)

    // ---- 内部状态 ----
    property var currentPoints: []
    property bool drawing: false
    property point dragStart: Qt.point(0, 0)
    property point dragEnd: Qt.point(0, 0)
    property point mousePos: Qt.point(0, 0)

    // 颜色常量
    readonly property color colorLine: "#00d4aa"
    readonly property color colorAngle: "#f59e0b"
    readonly property color colorRoi: "#3b82f6"
    readonly property color colorLabel: "#ffffff"
    readonly property color colorLabelBg: Qt.rgba(0, 0, 0, 0.75)
    readonly property color colorCrosshair: Qt.rgba(1, 1, 1, 0.6)

    // ---- 坐标映射工具函数 ----
    function getImageRect() {
        var imgW = Dicom.hasImage ? Dicom.imageWidth : 800;
        var imgH = Dicom.hasImage ? Dicom.imageHeight : 700;
        var viewW = root.width;
        var viewH = root.height;

        var imgAspect = imgW / imgH;
        var viewAspect = viewW / viewH;

        var displayW, displayH, offsetX, offsetY;

        if (imgAspect > viewAspect) {
            displayW = viewW;
            displayH = viewW / imgAspect;
            offsetX = 0;
            offsetY = (viewH - displayH) / 2;
        } else {
            displayH = viewH;
            displayW = viewH * imgAspect;
            offsetX = (viewW - displayW) / 2;
            offsetY = 0;
        }

        return { x: offsetX, y: offsetY, w: displayW, h: displayH,
                 imgW: imgW, imgH: imgH, margin: 4 };
    }

    function canvasToImage(px, py) {
        var r = getImageRect();
        // 考虑 Image 组件的 4px margin
        var m = 4;
        return {
            x: ((px - m - r.x) / (r.w - m * 2)) * r.imgW,
            y: ((py - m - r.y) / (r.h - m * 2)) * r.imgH
        };
    }

    function pixelToMm(px) {
        return px * root.pixelSpacing;
    }

    function calcDistance(p1, p2) {
        var dx = p1.x - p2.x;
        var dy = p1.y - p2.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function calcAngle(p1, p2, p3) {
        var a = Math.atan2(p1.y - p2.y, p1.x - p2.x);
        var b = Math.atan2(p3.y - p2.y, p3.x - p2.x);
        var angle = Math.abs(a - b) * 180 / Math.PI;
        if (angle > 180) angle = 360 - angle;
        return angle;
    }

    // ---- Canvas ----
    Canvas {
        id: canvas
        anchors.fill: parent

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            // 绘制已完成的测量
            for (var i = 0; i < measurementModel.count; i++) {
                var m = measurementModel.get(i);
                drawMeasurement(ctx, m);
            }

            // 绘制进行中的测量
            if (root.toolMode > 0 && root.toolMode < 3) {
                drawInProgress(ctx);
            }
        }

        function drawMeasurement(ctx, m) {
            var color;
            switch (m.type) {
                case "distance": color = colorLine; break;
                case "angle": color = colorAngle; break;
                case "roi": color = colorRoi; break;
                default: color = colorLine;
            }

            ctx.save();
            ctx.strokeStyle = color;
            ctx.fillStyle = color;
            ctx.lineWidth = 1.5;
            ctx.setLineDash([]);

            switch (m.type) {
                case "distance":
                    drawDistanceLine(ctx, m);
                    break;
                case "angle":
                    drawAngleShape(ctx, m);
                    break;
                case "roi":
                    drawRoiRect(ctx, m);
                    break;
            }

            ctx.restore();

            // 绘制标签
            drawLabel(ctx, m);
        }

        function drawDistanceLine(ctx, m) {
            ctx.beginPath();
            ctx.moveTo(m.p1x, m.p1y);
            ctx.lineTo(m.p2x, m.p2y);
            ctx.stroke();

            // 端点十字
            drawEndpoint(ctx, m.p1x, m.p1y, m.lineColor);
            drawEndpoint(ctx, m.p2x, m.p2y, m.lineColor);
        }

        function drawAngleShape(ctx, m) {
            // 两条边
            ctx.strokeStyle = m.lineColor;
            ctx.beginPath();
            ctx.moveTo(m.p1x, m.p1y);
            ctx.lineTo(m.vertexX, m.vertexY);
            ctx.moveTo(m.p3x, m.p3y);
            ctx.lineTo(m.vertexX, m.vertexY);
            ctx.stroke();

            // 角度弧线
            var startAngle = Math.atan2(m.p1y - m.vertexY, m.p1x - m.vertexX);
            var endAngle = Math.atan2(m.p3y - m.vertexY, m.p3x - m.vertexX);
            var diff = endAngle - startAngle;
            if (diff < -Math.PI) diff += 2 * Math.PI;
            if (diff > Math.PI) diff -= 2 * Math.PI;
            var counterclockwise = diff < 0;
            var arcRadius = Math.min(30, root.width * 0.08);

            ctx.strokeStyle = m.lineColor;
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.arc(m.vertexX, m.vertexY, arcRadius, startAngle, endAngle, counterclockwise);
            ctx.stroke();
        }

        function drawRoiRect(ctx, m) {
            var x = Math.min(m.x1, m.x2);
            var y = Math.min(m.y1, m.y2);
            var w = Math.abs(m.x2 - m.x1);
            var h = Math.abs(m.y2 - m.y1);

            ctx.strokeStyle = m.lineColor;
            ctx.lineWidth = 1.5;
            ctx.setLineDash([6, 3]);
            ctx.strokeRect(x, y, w, h);
            ctx.setLineDash([]);

            // 角标
            var corLen = 10;
            ctx.strokeStyle = m.lineColor;
            ctx.lineWidth = 2;
            // 左上
            ctx.beginPath(); ctx.moveTo(x, y + corLen); ctx.lineTo(x, y); ctx.lineTo(x + corLen, y); ctx.stroke();
            // 右上
            ctx.beginPath(); ctx.moveTo(x + w - corLen, y); ctx.lineTo(x + w, y); ctx.lineTo(x + w, y + corLen); ctx.stroke();
            // 左下
            ctx.beginPath(); ctx.moveTo(x, y + h - corLen); ctx.lineTo(x, y + h); ctx.lineTo(x + corLen, y + h); ctx.stroke();
            // 右下
            ctx.beginPath(); ctx.moveTo(x + w - corLen, y + h); ctx.lineTo(x + w, y + h); ctx.lineTo(x + w, y + h - corLen); ctx.stroke();
        }

        function drawEndpoint(ctx, x, y, color) {
            var s = 4;
            ctx.strokeStyle = color;
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(x - s, y);
            ctx.lineTo(x + s, y);
            ctx.moveTo(x, y - s);
            ctx.lineTo(x, y + s);
            ctx.stroke();
        }

        function drawLabel(ctx, m) {
            ctx.save();
            ctx.font = "bold 11px sans-serif";

            var text = m.value;
            var metrics = ctx.measureText(text);
            var tw = metrics.width + 12;
            var th = 18;

            var lx, ly;
            if (m.type === "distance") {
                lx = (m.p1x + m.p2x) / 2 - tw / 2;
                ly = (m.p1y + m.p2y) / 2 - th - 6;
            } else if (m.type === "angle") {
                lx = m.vertexX - tw / 2;
                ly = m.vertexY - th - 20;
            } else {
                var rx = Math.min(m.x1, m.x2);
                lx = rx + 4;
                ly = Math.max(m.y1, m.y2) + 4;
            }

            // 标签背景
            ctx.fillStyle = colorLabelBg;
            roundRect(ctx, lx, ly, tw, th, 6);

            // 标签文字
            ctx.fillStyle = m.lineColor;
            ctx.fillText(text, lx + 6, ly + 13);

            ctx.restore();
        }

        function roundRect(ctx, x, y, w, h, r) {
            ctx.beginPath();
            ctx.moveTo(x + r, y);
            ctx.lineTo(x + w - r, y);
            ctx.quadraticCurveTo(x + w, y, x + w, y + r);
            ctx.lineTo(x + w, y + h - r);
            ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
            ctx.lineTo(x + r, y + h);
            ctx.quadraticCurveTo(x, y + h, x, y + h - r);
            ctx.lineTo(x, y + r);
            ctx.quadraticCurveTo(x, y, x + r, y);
            ctx.closePath();
            ctx.fill();
        }

        function drawInProgress(ctx) {
            ctx.save();
            ctx.strokeStyle = colorCrosshair;
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);

            for (var i = 0; i < root.currentPoints.length; i++) {
                var pt = root.currentPoints[i];
                drawCross(ctx, pt.x, pt.y, i === 0 ? colorLine : colorAngle);
                if (i > 0) {
                    drawDashLine(ctx, root.currentPoints[i-1], pt);
                }
            }

            // 从最后一点到鼠标的虚线
            if (root.currentPoints.length > 0) {
                var last = root.currentPoints[root.currentPoints.length - 1];
                drawDashLine(ctx, last, root.mousePos);
            }

            ctx.setLineDash([]);
            ctx.restore();
        }

        function drawCross(ctx, x, y, c) {
            ctx.strokeStyle = c;
            ctx.lineWidth = 1.5;
            var s = 6;
            ctx.beginPath();
            ctx.moveTo(x - s, y); ctx.lineTo(x + s, y);
            ctx.moveTo(x, y - s); ctx.lineTo(x, y + s);
            ctx.stroke();
        }

        function drawDashLine(ctx, p1, p2) {
            ctx.strokeStyle = colorCrosshair;
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);
            ctx.beginPath();
            ctx.moveTo(p1.x, p1.y);
            ctx.lineTo(p2.x, p2.y);
            ctx.stroke();
        }
    }

    // ---- 交互 ----
    MouseArea {
        id: measureMouse
        anchors.fill: parent
        enabled: root.toolMode > 0
        cursorShape: root.toolMode > 0 ? Qt.CrossCursor : Qt.ArrowCursor
        hoverEnabled: root.toolMode === 1 || root.toolMode === 2

        onPositionChanged: {
            root.mousePos = Qt.point(mouse.x, mouse.y);
            if (root.toolMode === 3 && root.drawing) {
                root.dragEnd = Qt.point(mouse.x, mouse.y);
            }
            canvas.requestPaint();
        }

        onClicked: {
            if (root.toolMode === 1) {
                // 距离测量: 收集两点
                root.currentPoints.push(Qt.point(mouse.x, mouse.y));
                if (root.currentPoints.length >= 2) {
                    finalizeDistance();
                }
            } else if (root.toolMode === 2) {
                // 角度测量: 收集三点
                root.currentPoints.push(Qt.point(mouse.x, mouse.y));
                if (root.currentPoints.length >= 3) {
                    finalizeAngle();
                }
            }
        }

        onPressed: {
            if (root.toolMode === 3) {
                root.drawing = true;
                root.dragStart = Qt.point(mouse.x, mouse.y);
                root.dragEnd = Qt.point(mouse.x, mouse.y);
            }
        }

        onReleased: {
            if (root.toolMode === 3 && root.drawing) {
                root.dragEnd = Qt.point(mouse.x, mouse.y);
                finalizeRoi();
                root.drawing = false;
            }
        }
    }

    // ---- 测量完成处理 ----
    function finalizeDistance() {
        var p1 = root.currentPoints[0];
        var p2 = root.currentPoints[1];

        var imgP1 = canvasToImage(p1.x, p1.y);
        var imgP2 = canvasToImage(p2.x, p2.y);
        var pixelDist = calcDistance(imgP1, imgP2);
        var mmDist = pixelToMm(pixelDist);

        var value = pixelDist.toFixed(1) + " px | " + mmDist.toFixed(1) + " mm";
        measurementModel.append({
            type: "distance",
            p1x: p1.x, p1y: p1.y,
            p2x: p2.x, p2y: p2.y,
            value: value,
            lineColor: "#" + colorToHex(colorLine),
            rawPx: pixelDist,
            rawMm: mmDist
        });

        root.currentPoints = [];
        canvas.requestPaint();
    }

    function finalizeAngle() {
        var p1 = root.currentPoints[0];
        var p2 = root.currentPoints[1];  // vertex
        var p3 = root.currentPoints[2];

        var angle = calcAngle(p1, p2, p3);
        var value = angle.toFixed(1) + "°";

        measurementModel.append({
            type: "angle",
            p1x: p1.x, p1y: p1.y,
            vertexX: p2.x, vertexY: p2.y,
            p3x: p3.x, p3y: p3.y,
            value: value,
            lineColor: "#" + colorToHex(colorAngle),
            rawAngle: angle
        });

        root.currentPoints = [];
        canvas.requestPaint();
    }

    function finalizeRoi() {
        var x1 = root.dragStart.x;
        var y1 = root.dragStart.y;
        var x2 = root.dragEnd.x;
        var y2 = root.dragEnd.y;

        // 太小的 ROI 忽略
        if (Math.abs(x2 - x1) < 5 || Math.abs(y2 - y1) < 5) {
            root.drawing = false;
            return;
        }

        var imgP1 = canvasToImage(Math.min(x1, x2), Math.min(y1, y2));
        var imgP2 = canvasToImage(Math.max(x1, x2), Math.max(y1, y2));
        var w = Math.abs(imgP2.x - imgP1.x);
        var h = Math.abs(imgP2.y - imgP1.y);
        var areaPx = w * h;
        var areaMm2 = areaPx * root.pixelSpacing * root.pixelSpacing;

        var value = w.toFixed(0) + "×" + h.toFixed(0) + " px | " + areaMm2.toFixed(1) + " mm²";

        measurementModel.append({
            type: "roi",
            x1: x1, y1: y1,
            x2: x2, y2: y2,
            value: value,
            lineColor: "#" + colorToHex(colorRoi),
            rawAreaPx: areaPx,
            rawAreaMm2: areaMm2
        });

        root.drawing = false;
        root.dragStart = Qt.point(0, 0);
        root.dragEnd = Qt.point(0, 0);
        canvas.requestPaint();
    }

    function colorToHex(c) {
        var r = Math.round(c.r * 255).toString(16).padStart(2, '0');
        var g = Math.round(c.g * 255).toString(16).padStart(2, '0');
        var b = Math.round(c.b * 255).toString(16).padStart(2, '0');
        return r + g + b;
    }

    // ---- 外部接口 ----
    function clearAll() {
        root.currentPoints = [];
        root.drawing = false;
        measurementModel.clear();
        canvas.requestPaint();
    }

    function removeMeasurement(index) {
        measurementModel.remove(index);
        canvas.requestPaint();
    }

    // 当模式切换时清除进行中的测量
    onToolModeChanged: {
        root.currentPoints = [];
        root.drawing = false;
        root.dragStart = Qt.point(0, 0);
        root.dragEnd = Qt.point(0, 0);
        canvas.requestPaint();
    }

    // 测量列表变化时重绘
    Connections {
        target: measurementModel
        function onCountChanged() { canvas.requestPaint(); }
    }
}
