#ifndef LWPG_NOTIFIES_H
#define LWPG_NOTIFIES_H

#include <memory>

#include <libpq-fe.h>

#include "lwpg_conn.h"

namespace lwpg
{

    class Notify
    {
        PGnotify *notify = nullptr;

    public:
        Notify(const Notify &other) = delete;

        Notify(PGnotify *notify)
            : notify(notify)
        {}

        ~Notify()
        {
            if (!this->notify)
                return;

            PQfreemem(this->notify);
        }

        PGnotify *get()
        {
            return this->notify;
        }

        std::string relname() const
        {
            return std::string(this->notify->relname);
        }

        int be_pid() const
        {
            return this->notify->be_pid;
        }

        std::string extra() const
        {
            return std::string(this->notify->extra);
        }
    };

    std::shared_ptr<Notify> notifies(const std::shared_ptr<Conn> &conn)
    {
        return std::make_shared<Notify>(PQnotifies(conn->get()));
    }
}

#endif // LWPG_NOTIFIES_H
