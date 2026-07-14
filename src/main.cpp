#include <exception>
#include <iostream>

#include "Game/Game.h"

int main()
{
    try
    {
        Game game;
        game.run();
    }
    catch (const std::exception& e)
    {
        // Window creation failure lands here: report it and exit non-zero.
        std::cerr << "Litharia failed to start: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
