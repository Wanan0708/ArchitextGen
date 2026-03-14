/**
 * @file {{CLASS_NAME}}.h
 * @brief {{CLASS_NAME}} - Hardware Abstraction Layer
 * @domain {{DOMAIN_KEY}}
 * 
 * This class provides abstraction for hardware peripherals,
 * including sensor interfaces, actuator controls, and register mappings.
 */
#ifndef {{INCLUDE_GUARD}}
#define {{INCLUDE_GUARD}}

#include <QObject>
#include <QVariant>
#include <cstdint>

{{BASE_CLASS_INCLUDE}}

/**
 * @class {{CLASS_NAME}}
 * @brief Hardware abstraction for [specific hardware component]
 * 
 * This class is part of the {{DOMAIN_KEY}} domain and provides
 * a unified interface for hardware operations.
 */
class {{CLASS_NAME}} : public {{BASE_CLASS}}
{
    Q_OBJECT
{{CLASS_BODY}}
};

#endif // {{INCLUDE_GUARD}}
