#include <iostream>
#include "script.hpp"
#include <deque>
#include <vector>
#include <random>
#include <chrono>
#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>

#define TILE_SIZE 16
const sf::Vector2i size = {20, 20};
const enum{UP, RIGHT, DOWN, LEFT}snake_dir = UP;
std::deque<sf::Vector2i> snake_pos;
std::vector<sf::Vector2i> apple_pos;
sf::RenderWindow window;
sf::Event event;
sf::RectangleShape rect;
std::random_device rd;
std::mt19937 eng(rd());
std::uniform_int_distribution<> dX(0, size.x-1);
std::uniform_int_distribution<> dY(0, size.y-1);

void getMapWidth(Script* s, Line& l)
{
    s->funcReturn(size.x, l);
}

void getMapHeight(Script* s, Line& l)
{
    s->funcReturn(size.y, l);
}

void pushSnakePos(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;

    int type;
    const void* p;
    int vi[2];
    for(size_t i = 0; i < 2; ++i)
    {
        p = s->getValueContent(l.params[i], type);
        if(p == nullptr)
        {
            s->setError();
            return;
        }
        switch(type)
        {
            case INT: vi[i] = *(int*)p; break;
            case FLOAT: vi[i] = (int)*(float*)p; break;
            default: s->setError(); return;
        }
    }
    snake_pos.push_front({vi[0], vi[1]});
}

void checkKey(Script* s, Line& l)
{
    int tmp;
    const void* p = s->getValueContent(l.params[0], tmp);
    if(p == nullptr)
    {
        s->setError();
        return;
    }
    switch(tmp)
    {
        case INT: tmp = *(int*)p; break;
        case FLOAT: tmp = (int)*(float*)p; break;
        default: s->setError(); return;
    }

    switch(tmp)
    {
        case 0: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::Up); break;
        case 1: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::Right); break;
        case 2: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::Down); break;
        case 3: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::Left); break;
        case 4: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::Escape); break;
        case 5: tmp = sf::Keyboard::isKeyPressed(sf::Keyboard::R); break;
        default: tmp = 0; break;
    }

    s->funcReturn(tmp, l);
}

void initWindow(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    int tmp;
    const void* p = s->getValueContent(l.params[0], tmp);
    if(p == nullptr)
    {
        s->setError();
        return;
    }
    switch(tmp)
    {
        case INT: tmp = *(int*)p; break;
        case FLOAT: tmp = (int)*(float*)p; break;
        default: s->setError(); return;
    }
    window.create(sf::VideoMode(size.x*TILE_SIZE, size.y*TILE_SIZE), "Snake");
    window.setFramerateLimit(tmp);
    rect.setSize({(float)TILE_SIZE, (float)TILE_SIZE});
}

void isWindowOpen(Script* s, Line& l)
{
    s->funcReturn(window.isOpen(), l);
}

void closeWindow(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    window.close();
}

void pollEvent(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    while(window.pollEvent(event))
    {
        if(event.type == sf::Event::Closed)
            window.close();
    }
}

void draw(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    window.clear(sf::Color::Black);
    for(auto i = 0; i < snake_pos.size(); ++i)
    {
        if(i % 2 == 0) rect.setFillColor(sf::Color::Green);
        else rect.setFillColor(sf::Color::Yellow);
        rect.setPosition(snake_pos[i].x*TILE_SIZE, snake_pos[i].y*TILE_SIZE);
        window.draw(rect);
    }
    rect.setFillColor(sf::Color::Red);
    for(auto& vi: apple_pos)
    {
        rect.setPosition(vi.x*TILE_SIZE, vi.y*TILE_SIZE);
        window.draw(rect);
    }
    window.display();
}

void spawnApple(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    sf::Vector2i ap;
    bool b;
    do
    {
        ap = {dX(eng), dY(eng)};
        b = false;
        for(auto &x: apple_pos)
        {
            if(ap == x)
            {
                b = true;
                break;
            }
        }
        if(!b)
        {
            for(auto &x: snake_pos)
            {
                if(ap == x)
                {
                    b = true;
                    break;
                }
            }
        }
    }while(b);
    apple_pos.push_back(ap);
}

void eatApple(Script* s, Line& l)
{
    sf::Vector2i& head = snake_pos.front();
    for(size_t i = 0; i < apple_pos.size(); ++i)
    {
        if(apple_pos[i] == head)
        {
            apple_pos.erase(apple_pos.begin()+i);
            s->funcReturn(1, l);
            return;
        }
    }
    s->funcReturn(0, l);
}

void updateSnakeLenght(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;

    int sl;
    const void* p = s->getValueContent(l.params[0], sl);
    if(p == nullptr)
    {
        s->setError();
        return;
    }
    switch(sl)
    {
        case INT: sl = *(int*)p; break;
        case FLOAT: sl = (int)*(float*)p; break;
        default: s->setError(); return;
    }
    while(snake_pos.size() > sl)
        snake_pos.pop_back();
}

void checkGameOver(Script* s, Line& l)
{
    sf::Vector2i& ref = snake_pos[0];
    for(size_t i = 4; i < snake_pos.size(); ++i)
    {
        if(ref == snake_pos[i])
        {
            s->funcReturn(1, l);
            return;
        }
    }
    s->funcReturn(0, l);
}

void clearApples(Script* s, Line& l)
{
    if(s->rejectReturn(l)) return;
    apple_pos.clear();
}

int main()
{
    Script::addGlobalFunction("getMapWidth", getMapWidth, 0);
    Script::addGlobalFunction("getMapHeight", getMapHeight, 0);
    Script::addGlobalFunction("pushSnakePos", pushSnakePos, 2);
    Script::addGlobalFunction("checkKey", checkKey, 1);
    Script::addGlobalFunction("initWindow", initWindow, 1);
    Script::addGlobalFunction("isWindowOpen", isWindowOpen, 0);
    Script::addGlobalFunction("closeWindow", closeWindow, 0);
    Script::addGlobalFunction("pollEvent", pollEvent, 0);
    Script::addGlobalFunction("draw", draw, 0);
    Script::addGlobalFunction("spawnApple", spawnApple, 0);
    Script::addGlobalFunction("eatApple", eatApple, 0);
    Script::addGlobalFunction("updateSnakeLenght", updateSnakeLenght, 1);
    Script::addGlobalFunction("checkGameOver", checkGameOver, 0);
    Script::addGlobalFunction("clearApples", clearApples, 0);
    Script::initGlobalVariables(10);

    auto s = std::chrono::steady_clock::now();
    if(!Script::compile("snake.txt", "snake.csr"))
        return 0;
    auto e = std::chrono::steady_clock::now();
    std::cout << "Compile Time: " << std::chrono::duration<double, std::micro>(e-s).count() << " us" << std::endl;
    Script game;
    if(!game.load("snake.csr"))
        return 0;

    game.run();

    Script::clearGlobalVariables();

    return 0;
}
