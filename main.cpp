/**
 * ======================================================================================
 * 3D CLOTH SIMULATION (Verlet Integration)
 * ======================================================================================
 *
 * Concept:
 * This simulation uses a "Mass-Spring" model.
 * - MASS:   Represented by 'Points' (particles).
 * - SPRING: Represented by 'Links' (constraints keeping points at fixed distance).
 *
 * ASCII Visualization of the Grid:
 *
 * P ― Link ― P ― Link ― P
 * |          |          |
 * Link      Link       Link
 * |          |          |
 * P ― Link ― P ― Link ― P
 *
 * P = Point (Particle)
 * | = Vertical Link
 * ― = Horizontal Link
 *
 * ======================================================================================
 */

#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <iostream>

// --- Configuration Constants ---
const int WIDTH = 70;        // Number of points horizontally
const int HEIGHT = 45;       // Number of points vertically
const float DISTANCE = 18.f; // Resting distance between points
const float GRAVITY = 0.35f; // Downward force per frame
const float AIR_FRICTION = 0.98f; // Velocity damping factor. Lower value = more drag.
const float STRETCH_LIMIT = 5.0f; // Multiplier for link breaking threshold

/**
 * ------------------------------------------------------------------
 * STRUCT: Point
 * Represents a single particle in the cloth mesh.
 * ------------------------------------------------------------------
 *
 * VERLET INTEGRATION EXPLAINED:
 * Instead of storing velocity explicitly, we store the previous position.
 * Velocity is implicitly derived:
 *
 * PrevPos        CurrentPos        NextPos
 * O ―――――――――――> O ―――――――――――> O
 * ^                 ^
 * (Pos - Prev)      Apply this delta
 * is the vector     to current pos
 *
 * ------------------------------------------------------------------
 */
struct Point
{
    sf::Vector3f pos;       // Current Position (x, y, z)
    sf::Vector3f prevPos;   // Position in the previous frame
    bool locked = false;    // If true, the point is pinned (static)
    bool isGrabbed = false; // If true, currently held by mouse

    Point(float x, float y, float z) : pos(x, y, z), prevPos(x, y, z) {}

    void update(float time)
    {
        if (locked || isGrabbed)
            return;

        // 1. Calculate Velocity (Verlet)
        sf::Vector3f vel = (pos - prevPos) * AIR_FRICTION;

        // 2. Update Positions
        prevPos = pos;
        pos += vel;
        pos.y += GRAVITY; // Apply gravity force

        // 3. Simulate Wind (Sine wave on Z-axis)
        //    Adds a subtle oscillation to make it look alive.
        pos.z += std::sin(time + pos.x * 0.05f) * 0.15f;
        pos.z *= 0.99f; // Damping on Z to prevent infinite oscillation
    }
};

/**
 * ------------------------------------------------------------------
 * STRUCT: Link
 * Represents the constraint (stick) between two points.
 * ------------------------------------------------------------------
 *
 * CONSTRAINT SOLVING:
 * We want the distance (d) between P1 and P2 to always equal targetDist.
 * If (d != targetDist), we push/pull P1 and P2 to fix it.
 *
 * P1 <---- (correction) ----> P2
 *
 * ------------------------------------------------------------------
 */
struct Link
{
    Point *p1;
    Point *p2;
    float targetDist;    // The resting length of the link
    bool broken = false; // True if the link has been cut or snapped

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

        // --- TEAR LOGIC ---
        // If stretched too far (5x length), the link snaps.
        if (dist > targetDist * STRETCH_LIMIT)
        {
            broken = true;
            return;
        }

        // Avoid division by zero
        if (dist < 0.1f)
            return;

        // Calculate the correction factor
        // (Difference between current dist and target dist)
        float factor = (targetDist - dist) / dist * 0.5f; // 0.5 because each point moves half the error
        sf::Vector3f offset = diff * factor;

        // Apply correction if points are not locked/grabbed
        if (!p1->locked && !p1->isGrabbed)
            p1->pos += offset;
        if (!p2->locked && !p2->isGrabbed)
            p2->pos -= offset;
    }
};

/**
 * ------------------------------------------------------------------
 * FUNCTION: Project
 * Converts 3D World Coordinates (x,y,z) to 2D Screen Coordinates (x,y).
 * ------------------------------------------------------------------
 *
 * Eye/Camera
 * O
 * \
 * \   Screen Plane
 * \       |
 * \      |
 * \     v  (Projected Point)
 * \____.
 * \   |
 * \  |
 * \ |
 * \|
 * O (Actual 3D Point)
 *
 * Formula: screen_x = x * (focalLength / (focalLength + z))
 * ------------------------------------------------------------------
 */
sf::Vector2f project(sf::Vector3f p, sf::Vector2u winSize)
{
    float focalLength = 900.f;
    // Perspective division: Things further away (high Z) get smaller.
    // +500.f is the camera offset (distance from the cloth).
    float perspective = focalLength / (focalLength + p.z + 500.f);

    return {
        winSize.x / 2.f + p.x * perspective, // Center X
        winSize.y / 10.f + p.y * perspective // Offset Y slightly
    };
}

/**
 * ------------------------------------------------------------------
 * FUNCTION: Intersects
 * Checks if two 2D line segments intersect. Used for "cutting" links.
 * ------------------------------------------------------------------
 */
bool intersects(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d)
{
    // CCW (Counter-Clockwise) helper function
    auto ccw = [](sf::Vector2f p0, sf::Vector2f p1, sf::Vector2f p2)
    {
        return (p2.y - p0.y) * (p1.x - p0.x) > (p1.y - p0.y) * (p2.x - p0.x);
    };
    return ccw(a, c, d) != ccw(b, c, d) && ccw(a, b, c) != ccw(a, b, d);
}

// ======================================================================================
// MAIN FUNCTION
// ======================================================================================
int main()
{
    // 1. Setup Window
    sf::RenderWindow window(sf::VideoMode(1400, 900), "SFML 3D Cloth Simulation");
    window.setFramerateLimit(60);

    sf::Clock clock;
    std::vector<Point> points;
    std::vector<Link> links;

    // Interaction State
    Point *grabbedPoint = nullptr;
    sf::Vector2f lastMousePos;

    // 2. Initialize Points (Grid)
    //    Loops Y then X to create the mesh
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            // Center the cloth horizontally
            points.emplace_back(x * DISTANCE - (WIDTH * DISTANCE) / 2.f, y * DISTANCE, 0.f);

            // Pin the top row so the cloth hangs
            if (y == 0)
                points.back().locked = true;
        }
    }

    // 3. Initialize Links (Connections)
    //    Connects right (x+1) and down (y+1)
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (x < WIDTH - 1) // Link to Right
                links.emplace_back(points[y * WIDTH + x], points[y * WIDTH + x + 1]);

            if (y < HEIGHT - 1) // Link Down
                links.emplace_back(points[y * WIDTH + x], points[(y + 1) * WIDTH + x]);
        }
    }

    // 4. Main Game Loop
    while (window.isOpen())
    {
        float elapsed = clock.getElapsedTime().asSeconds();
        sf::Vector2u winSize = window.getSize();
        sf::Vector2f mPos = sf::Vector2f(sf::Mouse::getPosition(window));

        // --- Event Polling ---
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            // Handle Mouse Click (Grabbing)
            if (event.type == sf::Event::MouseButtonPressed)
            {
                if (event.mouseButton.button == sf::Mouse::Left)
                {
                    // Find the nearest point to the mouse cursor
                    float minDist = 50.f; // interaction radius
                    for (auto &p : points)
                    {
                        sf::Vector2f proj = project(p.pos, winSize);
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

            // Handle Mouse Release
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

        // --- Logic: Dragging Points ---
        if (grabbedPoint)
        {
            // Reverse projection to move 3D point with 2D mouse
            float focalLength = 900.f;
            float perspective = focalLength / (focalLength + grabbedPoint->pos.z + 500.f);

            grabbedPoint->pos.x = (mPos.x - winSize.x / 2.f) / perspective;
            grabbedPoint->pos.y = (mPos.y - winSize.y / 10.f) / perspective;

            // Reset velocity when dragging (prevent slingshot effect)
            grabbedPoint->prevPos = grabbedPoint->pos;
        }

        // --- Logic: Cutting Links (Right Click) ---
        if (sf::Mouse::isButtonPressed(sf::Mouse::Right))
        {
            for (auto &l : links)
            {
                sf::Vector2f p1 = project(l.p1->pos, winSize);
                sf::Vector2f p2 = project(l.p2->pos, winSize);

                // If the mouse trail intersects the link line, break it
                if (intersects(lastMousePos, mPos, p1, p2))
                {
                    l.broken = true;
                }
            }
        }

        // --- Logic: Physics Solver ---
        // Iterate multiple times per frame for stability (stiffer cloth)
        // 1 iteration = rubbery/stretchy
        // 8 iterations = rigid cloth
        for (int i = 0; i < 8; i++)
        {
            for (auto &l : links)
                l.solve();
        }

        // Remove broken links from the vector efficiently
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [](const Link &l)
                                   { return l.broken; }),
                    links.end());

        // Update individual point physics (gravity, wind)
        for (auto &p : points)
            p.update(elapsed * 1.5f);

        lastMousePos = mPos;

        // --- Rendering ---
        window.clear(sf::Color(10, 10, 15)); // Dark Blue/Grey background

        // Use VertexArray for high performance rendering of many lines
        sf::VertexArray va(sf::Lines);
        for (const auto &l : links)
        {
            sf::Vector2f v1 = project(l.p1->pos, winSize);
            sf::Vector2f v2 = project(l.p2->pos, winSize);

            // Depth Shading:
            // Calculate color based on Z-depth (closer = brighter, further = darker)
            float depth = std::max(0.f, std::min(1.f, (l.p1->pos.z + 100.f) / 400.f));
            std::uint8_t colorVal = static_cast<std::uint8_t>(255 * (1.0f - depth));

            // Set color (Yellow if grabbed, Blue-ish otherwise)
            sf::Color col = l.p1->isGrabbed ? sf::Color::Yellow : sf::Color(50, colorVal, 255);

            va.append(sf::Vertex(v1, col));
            va.append(sf::Vertex(v2, col));
        }

        window.draw(va);
        window.display();
    }

    return 0;
}