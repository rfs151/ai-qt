#pragma once

#include <QPixmap>
#include <QWidget>

class QTimer;

// QPainter 自绘的圆形仪表：240° 表盘、正常/警告/告警三段色带、刻度、指针缓动。
// 表盘（色带、刻度、数字）只在尺寸或量程变化时画一次缓存成 QPixmap，
// 之后每帧只画指针和读数。
class GaugeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GaugeWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setUnit(const QString &unit);
    void setRange(double min, double max);
    void setThresholds(double warn, double alarm);
    void setDecimals(int d) { m_decimals = d; }

    // 设置目标值，指针会平滑转过去；hasValue 为 false 时显示 "--"（设备离线/无数据）
    void setValue(double v);
    void clearValue();
    double displayedValue() const { return m_shown; }

    QSize sizeHint() const override { return {170, 170}; }
    QSize minimumSizeHint() const override { return {110, 110}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void rebuildDial();
    void animate();
    double angleFor(double v) const;   // 数值 -> 角度（度，Qt 坐标系逆时针）
    QColor levelColor(double v) const;

    QString m_title;
    QString m_unit;
    double m_min = 0;
    double m_max = 100;
    double m_warn = 80;
    double m_alarm = 95;
    int m_decimals = 1;

    bool m_hasValue = false;
    double m_target = 0;
    double m_shown = 0;
    QTimer *m_anim;

    QPixmap m_dial;   // 缓存的静态表盘
    bool m_dialDirty = true;
};
