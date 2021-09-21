
#ifndef CPPFS_API_H
#define CPPFS_API_H

#ifdef CPPFS_STATIC_DEFINE
#  define CPPFS_API
#  define CPPFS_NO_EXPORT
#else
#  ifndef CPPFS_API
#    ifdef cppfs_EXPORTS
        /* We are building this library */
#      define CPPFS_API 
#    else
        /* We are using this library */
#      define CPPFS_API 
#    endif
#  endif

#  ifndef CPPFS_NO_EXPORT
#    define CPPFS_NO_EXPORT 
#  endif
#endif

#ifndef CPPFS_DEPRECATED
#  define CPPFS_DEPRECATED __attribute__ ((__deprecated__))
#endif

#ifndef CPPFS_DEPRECATED_EXPORT
#  define CPPFS_DEPRECATED_EXPORT CPPFS_API CPPFS_DEPRECATED
#endif

#ifndef CPPFS_DEPRECATED_NO_EXPORT
#  define CPPFS_DEPRECATED_NO_EXPORT CPPFS_NO_EXPORT CPPFS_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CPPFS_NO_DEPRECATED
#    define CPPFS_NO_DEPRECATED
#  endif
#endif

#endif /* CPPFS_API_H */
