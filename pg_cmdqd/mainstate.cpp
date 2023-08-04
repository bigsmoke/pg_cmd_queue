#include "mainstate.h"

void MainState::refresh()
{
    std::lock_guard<std::mutex> locker(refreshMutex);

    try
    {
        LWPGcontext pg;
        pg.connectdb((getConnectionString());
    }
}
