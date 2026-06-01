#include <pthread.h>

#include "pal/pal_thread.hpp"

namespace
{

struct TrampolineArg
{
    pal::ThreadFn fn;
    void *user;
    char name[16]; // pthread_setname_np limit is 15 + NUL
};

void *trampoline(void *raw)
{
    TrampolineArg *arg = static_cast<TrampolineArg *>(raw);
    pthread_setname_np(pthread_self(), arg->name);
    pal::ThreadFn fn = arg->fn;
    void *user = arg->user;
    delete arg;
    fn(user);
    return nullptr;
}

} // namespace

namespace pal
{

void spawn_thread(const char *name, ThreadFn fn, void *arg)
{
    TrampolineArg *wrap = new TrampolineArg{fn, arg, {}};
    for (int i = 0; i < 15 && name && name[i]; ++i)
        wrap->name[i] = name[i];

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, &trampoline, wrap);
    pthread_attr_destroy(&attr);
}

} // namespace pal
