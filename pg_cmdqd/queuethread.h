#ifndef QUEUETHREAD_H
#define QUEUETHREAD_H

#define MAX_EVENTS 20

class QueueThread
{
    bool _keep_running = true;

public:
    QueueThread() = default;
    ~QueueThread() = default;

    void run();
    void stop_running();
};

#endif // QUEUETHREAD_H
