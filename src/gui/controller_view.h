#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <array>

namespace sc2 {

// 3D wireframe view of the Steam Controller 2.
//
// Orientation is derived from live DSU-space accel/gyro via a complementary
// filter: accelerometer gives pitch and roll; gyroscope integrates yaw and
// smooths transients.
//
// DSU-space conventions assumed here (see triton.h):
//   Accel X positive  = AccelRightToLeft  → right side down when +X
//   Accel Y positive  = AccelFrontToBack  → face down when +Y
//   Accel Z positive  = AccelTopToBottom  → flat face-up gives accel_z ≈ +1
//   Gyro  Z positive  = GyroTopToBottom   → clockwise spin (viewed from top) is +
class ControllerView : public QWidget {
    Q_OBJECT
public:
    explicit ControllerView(QWidget* parent = nullptr);

    // Feed a new DSU-space sample. Call from main thread.
    // accel[] in g, gyro[] in °/s, elapsed_us = hardware timestamp delta.
    void pushSample(float accel[3], float gyro[3], quint64 elapsed_us);

    // Reset filter state (e.g. when slot changes or stream reconnects).
    void resetFilter();

    QSize sizeHint() const override { return {360, 280}; }
    QSize minimumSizeHint() const override { return {200, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    struct V3 { float x, y, z; };

    // Complementary filter state
    float   roll_  = 0.f;   // radians, around Z (face-toward-camera axis)
    float   pitch_ = 0.f;   // radians, around X (left-right axis)
    float   yaw_   = 0.f;   // radians, around Y (up axis)  [integrated only]
    bool    filterInit_ = false;

    // For display
    float lastAccel_[3] = {};
    float lastGyro_[3]  = {};

    // 3D geometry helpers
    V3   rotate(V3 v) const;
    QPointF project(V3 v, float cx, float cy, float scale) const;
    void drawLine(class QPainter& p, V3 a, V3 b, float cx, float cy, float scale) const;
    void drawCircle(class QPainter& p, V3 center, float r, V3 normalAxis,
                    float cx, float cy, float scale, int segs = 12) const;
};

} // namespace sc2
