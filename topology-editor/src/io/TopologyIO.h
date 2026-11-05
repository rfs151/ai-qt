#pragma once

#include <QByteArray>
#include <QString>

class TopologyScene;

namespace TopologyIO {

QByteArray toJson(const TopologyScene *scene);
// 成功返回 true；失败时场景不变，错误写入 error
bool fromJson(TopologyScene *scene, const QByteArray &data, QString *error);

// 内置示例：核心-汇聚-接入三层园区网络
void buildSampleCampus(TopologyScene *scene);
// 压力测试：生成约 count 个节点、1.5 倍连线
void buildStressGrid(TopologyScene *scene, int count);

} // namespace TopologyIO
