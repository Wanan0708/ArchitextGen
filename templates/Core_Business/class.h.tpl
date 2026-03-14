/**
 * @file {{CLASS_NAME}}.h
 * @brief {{CLASS_NAME}} - Core Business Logic Class
 * @domain {{DOMAIN_KEY}}
 * 
 * This class implements core business logic for the application.
 * It handles business rules, data processing, and domain operations.
 */
#ifndef {{INCLUDE_GUARD}}
#define {{INCLUDE_GUARD}}

#include <QObject>
#include <QVariant>
#include <QList>

{{BASE_CLASS_INCLUDE}}

/**
 * @class {{CLASS_NAME}}
 * @brief Core business logic implementation
 * 
 * This class is part of the {{DOMAIN_KEY}} domain and contains
 * the primary business logic for the application.
 */
class {{CLASS_NAME}} : public {{BASE_CLASS}}
{
    Q_OBJECT
{{CLASS_BODY}}
};

#endif // {{INCLUDE_GUARD}}
