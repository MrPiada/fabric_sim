#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <optional>

const int WIDTH = 70;
const int HEIGHT = 45;
const float DISTANCE = 18.f;
const float GRAVITY = 0.35f;

struct Point
{
    sf::Vector3f pos;
    sf::Vector3f prevPos;
    bool locked = false;
    bool isGrabbed = false;

    Point(float x, float y, float z) : pos(x, y, z), prevPos(x, y, z) {}

    void update(float time)
    {
        if (locked || isGrabbed)
            return;

        sf::Vector3f vel = (pos - prevPos) * 0.98f;
        prevPos = pos;
        pos += vel;
        pos.y += GRAVITY;

        pos.z += std::sin(time + pos.x * 0.05f) * 0.15f;
        pos.z *= 0.99f;
    }
};

struct Link
{
    Point *p1;
    Point *p2;
    float targetDist;
    bool broken = false;

    Link(Point &a, Point &b) : p1(&a), p2(&b)
    {
        sf::Vector3f d = p1->pos - p2->pos;
        targetDist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    void solve()
    {
        if (broken)
            return;
        sf::Vector3f diff = p1->pos - p2->pos;
        float dist = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        if (dist > targetDist * 5.0f)
        {
            broken = true;
            return;
        }
        if (dist < 0.1f)
            return;

        float factor = (targetDist - dist) / dist * 0.5f;
        sf::Vector3f offset = diff * factor;
        if (!p1->locked && !p1->isGrabbed)
            p1->pos += offset;
        if (!p2->locked && !p2->isGrabbed)
            p2->pos -= offset;
    }
};

sf::Vector2f project(sf::Vector3f p, sf::Vector2u winSize)
{
    float focalLength = 900.f;
    float perspective = focalLength / (focalLength + p.z + 500.f);
    return {
        winSize.x / 2.f + p.x * perspective,
        winSize.y / 10.f + p.y * perspective};
}

bool intersects(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d)
{
    auto ccw = [](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2)
    {
        return (p2.y - p0.y) * (p1.x - p0.x) > (p1.y - p0.y) * (p2.x - p0.x);
    };
    return ccw(a, c, d) != ccw(b, c, d) && ccw(a, b, c) != ccw(a, b, d);
}

int main()
{
    sf::RenderWindow window(sf::VideoMode(1400, 900), "SFML 3D"); // Fixed VideoMode syntax
    window.setFramerateLimit(60);

    sf::Clock clock;
    std::vector<Point> points;
    std::vector<Link> links;
    Point *grabbedPoint = nullptr;
    sf::Vector2f lastMousePos;

    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            points.emplace_back(x * DISTANCE - (WIDTH * DISTANCE) / 2.f, y * DISTANCE, 0.f);
            if (y == 0)
                points.back().locked = true;
        }
    }

    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (x < WIDTH - 1)
                links.emplace_back(points[y * WIDTH + x], points[y * WIDTH + x + 1]);
            if (y < HEIGHT - 1)
                links.emplace_back(points[y * WIDTH + x], points[(y + 1) * WIDTH + x]);
        }
    }

    while (window.isOpen())
    {
        float elapsed = clock.getElapsedTime().asSeconds();
        sf::Vector2u winSize = window.getSize();
        sf::Vector2f mPos = sf::Vector2f(sf::Mouse::getPosition(window));

        // --- FIXED: SFML 2.x Event Loop ---
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::MouseButtonPressed)
            {
                if (event.mouseButton.button == sf::Mouse::Left)
                {
                    float minDist = 50.f;
                    for (auto &p : points)
                    {
                        sf::Vector2f proj = project(p.pos, winSize);
                        // std::hypot is C++17, usually fine, but manual dist is safer on old compilers
                        float dx = proj.x - mPos.x;
                        float dy = proj.y - mPos.y;
                        float d = std::sqrt(dx * dx + dy * dy);

                        if (d < minDist && !p.locked)
                        {
                            minDist = d;
                            grabbedPoint = &p;
                        }
                    }
                    if (grabbedPoint)
                        grabbedPoint->isGrabbed = true;
                }
            }

            if (event.type == sf::Event::MouseButtonReleased)
            {
                if (event.mouseButton.button == sf::Mouse::Left)
                {
                    if (grabbedPoint)
                    {
                        grabbedPoint->isGrabbed = false;
                        grabbedPoint = nullptr;
                    }
                }
            }
        }
        // ----------------------------------

        if (grabbedPoint)
        {
            float focalLength = 900.f;
            float perspective = focalLength / (focalLength + grabbedPoint->pos.z + 500.f);
            grabbedPoint->pos.x = (mPos.x - winSize.x / 2.f) / perspective;
            grabbedPoint->pos.y = (mPos.y - winSize.y / 10.f) / perspective;
            grabbedPoint->prevPos = grabbedPoint->pos;
        }

        if (sf::Mouse::isButtonPressed(sf::Mouse::Right))
        {
            for (auto &l : links)
            {
                sf::Vector2f p1 = project(l.p1->pos, winSize);
                sf::Vector2f p2 = project(l.p2->pos, winSize);
                if (intersects(lastMousePos, mPos, p1, p2))
                {
                    l.broken = true;
                }
            }
        }

        for (int i = 0; i < 8; i++)
        {
            for (auto &l : links)
                l.solve();
        }

        // --- FIXED: C++17 Compatible Erase-Remove ---
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [](const Link &l)
                                   { return l.broken; }),
                    links.end());
        // --------------------------------------------

        for (auto &p : points)
            p.update(elapsed * 1.5f);

        lastMousePos = mPos;

        window.clear(sf::Color(10, 10, 15));

        sf::VertexArray va(sf::Lines); // Fixed: sf::Lines instead of sf::PrimitiveType::Lines (SFML 2 compat)
        for (const auto &l : links)
        {
            sf::Vector2f v1 = project(l.p1->pos, winSize);
            sf::Vector2f v2 = project(l.p2->pos, winSize);

            float depth = std::max(0.f, std::min(1.f, (l.p1->pos.z + 100.f) / 400.f)); // std::clamp is C++17
            std::uint8_t colorVal = static_cast<std::uint8_t>(255 * (1.0f - depth));

            sf::Color col = l.p1->isGrabbed ? sf::Color::Yellow : sf::Color(50, colorVal, 255);
            va.append(sf::Vertex(v1, col));
            va.append(sf::Vertex(v2, col));
        }

        window.draw(va);
        window.display();
    }

    return 0;
}