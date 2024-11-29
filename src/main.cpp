#include "engine.hpp"

int main()
{
    Engine engine;

    while (engine.isRunning()) {
        engine.update();
    }

    return 0;
}
