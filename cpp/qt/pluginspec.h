#ifndef PLUGINSPEC_H
#define PLUGINSPEC_H

#include "extensionsystem_global.h"

#include <QString>
#include <QHash>
#include <QVector>

QT_BEGIN_NAMESPACE
class QStringList;
class QRegExp;
QT_END_NAMESPACE

namespace ExtensionSystem {

namespace Internal {

class PluginSpecPrivate;
class PluginManagerPrivate;

} // Internal

class IPlugin;

struct EXTENSIONSYSTEM_EXPORT PluginDependency
{
    enum Type {
        Required,
        Optional,
        Test
    };

    PluginDependency() : type(Required) {}

    QString name;
    QString version;
    Type type;
    bool operator==(const PluginDependency &other) const;
};

uint qHash(const ExtensionSystem::PluginDependency &value);

class EXTENSIONSYSTEM_EXPORT PluginSpec
{
public:
    enum State { Invalid, Read, Resolved, Loaded, Initialized, Running, Stopped, Deleted};

    ~PluginSpec();

    // information from the xml file, valid after 'Read' state is reached
    QString name() const;
    QString version() const;
    QString compatVersion() const;
    QString vendor() const;
    QString copyright() const;
    QString license() const;
    QString description() const;
    QString url() const;
    QString category() const;
    QRegExp platformSpecification() const;
    bool isAvailableForHostPlatform() const;
    bool isRequired() const;
    bool isExperimental() const;
    bool isEnabledByDefault() const;
    bool isEnabledBySettings() const;
    bool isEffectivelyEnabled() const;
    bool isEnabledIndirectly() const;
    bool isForceEnabled() const;
    bool isForceDisabled() const;
    QVector<PluginDependency> dependencies() const;


    // other information, valid after 'Read' state is reached
    QString location() const;
    QString filePath() const;

    bool provides(const QString &pluginName, const QString &version) const;

    // dependency specs, valid after 'Resolved' state is reached
    QHash<PluginDependency, PluginSpec *> dependencySpecs() const;

    // linked plugin instance, valid after 'Loaded' state is reached
    IPlugin *plugin() const;

    // state
    State state() const;
    bool hasError() const;
    QString errorString() const;

    void setEnabledBySettings(bool value);

private:
    PluginSpec();

    Internal::PluginSpecPrivate *d;
    friend class Internal::PluginManagerPrivate;
    friend class Internal::PluginSpecPrivate;
};

} // namespace ExtensionSystem

#endif // PLUGINSPEC_H

