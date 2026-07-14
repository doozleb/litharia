#include "Game.h"



Game::Game()
{
    window.create(
        sf::VideoMode({800,600}),
        "Litharia"
    );


    window.setFramerateLimit(60);


    cameraPosition = {0,0};


    world.generate();
}



void Game::run()
{
    while(window.isOpen())
    {

        while(auto event = window.pollEvent())
        {
            if(event->is<sf::Event::Closed>())
                window.close();
        }


        update();

        render();
    }
}



void Game::update()
{

}



void Game::render()
{
    window.clear(
        sf::Color(100,180,255)
    );


    world.draw(
        window,
        cameraPosition
    );


    window.display();
}