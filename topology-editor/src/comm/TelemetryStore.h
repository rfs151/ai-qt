#pragma once

#include "comm/CommTypes.h"

#include <QHash>
#include <QPointF>
#include <QVector>
#include <memory>
#include <vector>

// 定长环形缓冲：写满后覆盖最旧的数据，内存占用恒定，不会随运行时间增长
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(qsizetype capacity = 0) : m_data(size_t(capacity)) {}

    qsizetype capacity() const { return qsizetype(m_data.size()); }
    qsizetype size() const { return m_size; }
    bool isEmpty() const { return m_size == 0; }

    void push(const T &v)
    {
        if (m_data.empty())
            return;
        const qsizetype cap = capacity();
        if (m_size < cap) {
            m_data[size_t((m_start + m_size) % cap)] = v;
            ++m_size;
        } else {
            m_data[size_t(m_start)] = v;
            m_start = (m_start + 1) % cap;
        }
    }

    // 0 是最旧的
    const T &at(qsizetype i) const { return m_data[size_t((m_start + i) % capacity())]; }
    const T &back() const { return at(m_size - 1); }

    void clear()
    {
        m_start = 0;
        m_size = 0;
    }

    // 第一个 key(at(i)) >= k 的下标；要求按 key 有序（时间序列天然满足）
    template <typename K, typename Proj>
    qsizetype lowerBound(const K &k, Proj key) const
    {
        qsizetype lo = 0, hi = m_size;
        while (lo < hi) {
            const qsizetype mid = (lo + hi) / 2;
            if (key(at(mid)) < k)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

private:
    std::vector<T> m_data;
    qsizetype m_start = 0;
    qsizetype m_size = 0;
};

struct TelemetryPoint
{
    qint64 t;
    std::array<float, Proto::kChannelCount> v;
};
using TelemetrySeries = RingBuffer<TelemetryPoint>;

// 各设备的历史采样（界面线程使用）
class TelemetryStore
{
public:
    explicit TelemetryStore(qsizetype capacityPerDevice = 18000) : m_capacity(capacityPerDevice) {}

    void append(const Sample &s);
    const TelemetrySeries *series(int deviceId) const;
    void remove(int deviceId);
    void clear();

    // 整段替换某个设备的数据（历史回放用），容量等于点数
    void setSeries(int deviceId, const QVector<TelemetryPoint> &points);
    // 生成一段合成数据（曲线压测用）：count 个点，间隔 stepMs，最后一个点在 endMs
    void fillSynthetic(int deviceId, qsizetype count, qint64 endMs, int stepMs);

    quint64 version() const { return m_version; }   // 每次写入递增，曲线据此判断是否需要重绘
    qsizetype deviceCount() const { return m_series.size(); }

private:
    qsizetype m_capacity;
    QHash<int, std::shared_ptr<TelemetrySeries>> m_series;
    quint64 m_version = 0;
};

// 曲线降采样（min/max 抽取）：把 [t0, t1] 内的点映射到 width 个像素列，
// 每列只保留 首/最小/最大/尾 四个点，输出点数 ≤ 4×width，峰值不会丢。
// 时间间隔超过 gapMs 的地方断开，输出多段折线（掉线期间曲线留空）。
// 输出坐标：x 为相对像素列（0..width），y 为原始数值。
struct DecimateResult
{
    QVector<QVector<QPointF>> segments;
    qsizetype inputPoints = 0;
    qsizetype outputPoints = 0;
    double minY = 0;
    double maxY = 0;
};
DecimateResult decimateMinMax(const TelemetrySeries &s, int channel, qint64 t0, qint64 t1, int width, qint64 gapMs);
