#ifndef XMLHTTPREQUEST_GLOBAL_H
#define XMLHTTPREQUEST_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(XMLHTTPREQUEST_LIBRARY)
#  define XMLHTTPREQUESTSHARED_EXPORT Q_DECL_EXPORT
#else
#  define XMLHTTPREQUESTSHARED_EXPORT Q_DECL_IMPORT
#endif

#endif // XMLHTTPREQUEST_GLOBAL_H
