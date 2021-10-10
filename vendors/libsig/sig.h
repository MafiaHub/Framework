/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef __libsig__signal__h_
#define __libsig__signal__h_

#ifdef __cplusplus
#define __SIG_C_DECL extern "C"
#else
#define __SIG_C_DECL
#endif

#if defined(_WIN32)
#ifdef _libsig_dll
#define _libsig_export
#else
#define _libsig_export
#endif
#else
#define _libsig_export __attribute__((visibility("default")))
#endif

typedef const void *const sig_signal_id_t;
typedef void *sig_signal_object_t;
typedef struct sig_signal_s sig_signal_t;
typedef struct sig_context_s sig_context_t;

struct _libsig_export sig_context_s {
    const int ctx_id;
    int status;
#ifdef __cplusplus
    sig_context_s(int id);
#endif
};

__SIG_C_DECL _libsig_export const sig_context_t *sig_ctx_new();
__SIG_C_DECL _libsig_export void sig_ctx_free(const sig_context_t *ctx);

/* Predefined signal contexts */
__SIG_C_DECL _libsig_export const sig_context_t *sig_ctx_default();
__SIG_C_DECL _libsig_export const sig_context_t *sig_ctx_sys();

struct _libsig_export sig_signal_s {
    sig_signal_id_t signal_id;
    sig_signal_object_t object;
    const sig_context_t *context;
#ifdef __cplusplus
    sig_signal_s(sig_signal_id_t _signal_id, const sig_context_t *_context, const sig_signal_object_t _object): context(_context), signal_id(_signal_id), object(_object) {}
#endif
};

#define SIG_STATUS_T       int
#define SIG_STATUS_SUCCESS 0
#define SIG_STATUS_FAILURE 1

#ifdef __cplusplus
struct _libsig_export sig_mem_observer_base_t {
    virtual void operator()(const sig_signal_t signal) const  = 0;
    virtual bool operator==(sig_mem_observer_base_t *o) const = 0;
    virtual void *get_obj_addr() const                        = 0;
    virtual ~sig_mem_observer_base_t() {};
};

template <typename T>
_libsig_export sig_mem_observer_base_t *sig_make_mem_observer(T *inst, void (T::*memFn)(const sig_signal_t signal)) {
    struct sig_mem_observer_t: public sig_mem_observer_base_t {
        T *m_inst;

        void (T::*m_cb)(const sig_signal_t signal);
        void *get_obj_addr() const {
            return m_inst;
        }

        void operator()(const sig_signal_t signal) const {
            (m_inst->*m_cb)(signal);
        }

        bool operator==(sig_mem_observer_base_t *o) const {
            sig_mem_observer_t *observer2 = static_cast<sig_mem_observer_t *>(o);
            return m_inst == observer2->m_inst && m_cb == observer2->m_cb;
        }

        ~sig_mem_observer_t() {}
    };

    sig_mem_observer_t *_observer = new sig_mem_observer_t;
    _observer->m_inst             = inst;
    _observer->m_cb               = memFn;

    return _observer;
}

typedef sig_mem_observer_base_t *sig_observer_cb2_t;

#define sig_slot(t, f) sig_make_mem_observer(t, f)

#endif

typedef void (*sig_observer_cb_t)(const sig_signal_t signal);

/**
 * Attach a non-function or member-function to event
 */
__SIG_C_DECL _libsig_export void sig_attach(int signal, sig_observer_cb_t cb);

#ifdef __cplusplus
// Default context
_libsig_export void sig_attach(int signal, sig_observer_cb2_t cb);
_libsig_export void sig_attach(const char *signal, sig_observer_cb_t cb);
_libsig_export void sig_attach(const char *signal, sig_observer_cb2_t cb);

// Attach by given context
_libsig_export void sig_attach(int signal, sig_observer_cb_t cb, const sig_context_t *ctx);

_libsig_export void sig_attach(int signal, sig_observer_cb2_t cb, const sig_context_t *ctx);

_libsig_export void sig_attach(const char *signal, sig_observer_cb_t cb, const sig_context_t *ctx);

_libsig_export void sig_attach(const char *signal, sig_observer_cb2_t cb, const sig_context_t *ctx);

#else
__SIG_C_DECL _libsig_export void sig_attachc(int signal, sig_observer_cb_t cb, const sig_context_t *ctx);

__SIG_C_DECL _libsig_export void sig_attach_s(const char *signal, sig_observer_cb_t cb);

__SIG_C_DECL _libsig_export void sig_attachc_s(const char *signal, sig_observer_cb_t cb, const sig_context_t *ctx);
#endif

/**
 * Detach a non-function or member-function to event
 */
__SIG_C_DECL _libsig_export void sig_detach(int signal, sig_observer_cb_t cb);

#ifdef __cplusplus

// Default context
_libsig_export void sig_detach(int signal, sig_observer_cb2_t cb);
_libsig_export void sig_detach(const char *signal, sig_observer_cb_t cb);
_libsig_export void sig_detach(const char *signal, sig_observer_cb2_t cb);

// Default by given context
_libsig_export void sig_detach(int signal, sig_observer_cb_t cb, const sig_context_t *ctx);

_libsig_export void sig_detach(int signal, sig_observer_cb2_t cb, const sig_context_t *ctx);

_libsig_export void sig_detach(const char *signal, sig_observer_cb2_t cb, const sig_context_t *ctx);

_libsig_export void sig_detach(const char *signal, sig_observer_cb2_t cb, const sig_context_t *ctx);

// Detach all observer from the object/instance
// This can be useful when the object will be free-ing
_libsig_export void sig_detach(void *observer);

_libsig_export void sig_detach(void *observer, const sig_context_t *ctx);

// Detach all observers by given signal from the object/instance
_libsig_export void sig_detach(int signal, void *observer);
_libsig_export void sig_detach(const char *signal, void *observer);

_libsig_export void sig_detach(int signal, void *observer, const sig_context_t *ctx);

_libsig_export void sig_detach(int signal, void *observer, const sig_context_t *ctx);

#else
__SIG_C_DECL _libsig_export void sig_detachc(int signal, sig_observer_cb_t cb, const sig_context_t *ctx);

__SIG_C_DECL _libsig_export void sig_detach_s(const char *signal, sig_observer_cb_t cb);

__SIG_C_DECL _libsig_export void sig_detachc_s(int signal, sig_observer_cb_t cb, const sig_context_t *ctx);
#endif

/**
 * Fire an event by given name and object
 */
__SIG_C_DECL _libsig_export void sig_fire(int signal, void *object);

#ifdef __cplusplus
_libsig_export void sig_fire(const char *signal, void *object);

_libsig_export void sig_fire(int signal, void *object, const sig_context_t *ctx);

_libsig_export void sig_fire(const char *signal, void *object, const sig_context_t *ctx);
#else
__SIG_C_DECL _libsig_export void sig_firec(int signal, void *object, const sig_context_t *ctx);

__SIG_C_DECL _libsig_export void sig_fire_s(const char *signal, void *object);

__SIG_C_DECL _libsig_export void sig_firec_s(const char *signal, void *object, const sig_context_t *ctx);
#endif

#ifdef __cplusplus
namespace sig {

    struct _libsig_export attach_stream_ref {
        struct attach_stream;
        struct attach_stream_s;

        attach_stream operator[](int signal) const;
        attach_stream_s operator[](const char *signal) const;

        struct attach_stream {
            const int sig_id;
            attach_stream(const int _sig_id): sig_id(_sig_id) {}
            const attach_stream &operator<<(sig_observer_cb_t cb) const;
            const attach_stream &operator<<(sig_observer_cb2_t cb) const;
        };

        struct attach_stream_s {
            const char *sig_id;
            attach_stream_s(const char *_sig_id): sig_id(_sig_id) {}
            const attach_stream_s &operator<<(sig_observer_cb_t cb) const;
            const attach_stream_s &operator<<(sig_observer_cb2_t cb) const;
        };
    };

    struct _libsig_export detach_stream_ref {
        struct detach_stream;
        struct detach_stream_s;

        detach_stream operator[](int signal) const;
        detach_stream_s operator[](const char *signal) const;

        struct detach_stream {
            const int sig_id;
            detach_stream(const int _sig_id): sig_id(_sig_id) {}
            const detach_stream &operator>>(sig_observer_cb_t cb) const;
            const detach_stream &operator>>(sig_observer_cb2_t cb) const;
        };

        struct detach_stream_s {
            const char *sig_id;
            detach_stream_s(const char *_sig_id): sig_id(_sig_id) {}
            const detach_stream_s &operator>>(sig_observer_cb_t cb) const;
            const detach_stream_s &operator>>(sig_observer_cb2_t cb) const;
        };
    };

    struct _libsig_export fire_stream_ref {
        struct fire_stream;
        struct fire_stream_s;

        fire_stream operator[](int signal) const;
        fire_stream_s operator[](const char *signal) const;

        struct fire_stream {
            const int sig_id;
            fire_stream(const int _sig_id): sig_id(_sig_id) {}
            const fire_stream &operator<<(void *object) const;
        };

        struct fire_stream_s {
            const char *sig_id;
            fire_stream_s(const char *_sig_id): sig_id(_sig_id) {}
            const fire_stream_s &operator<<(void *object) const;
        };
    };

    extern _libsig_export attach_stream_ref attach;
    extern _libsig_export detach_stream_ref detach;
    extern _libsig_export fire_stream_ref fire;

}; // namespace sig

#endif

#endif /* defined(__libsig__signal__h_) */
