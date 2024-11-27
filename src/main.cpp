#include "engine.hpp"

int main()
{
    if (!Engine::init())
    {
        Engine::shutdown();
        return 1;
    }

    while (Engine::isRunning()) {
        Engine::update();
    }

    Engine::shutdown();
    return 0;
}
