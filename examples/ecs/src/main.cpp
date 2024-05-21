#include <entt/entity/registry.hpp>
#include <iostream>
#include <random>

#include "ecs/registry_wrapper.h"

struct Health {
    float life;
};

struct Position {
    float x, y;
};

void start(ecs_trial::Command command) {
    for (int i = 0; i < 500; i++) {
        using namespace std;
        int min = 0, max = 100;
        random_device seed;       // Ӳ���������������
        ranlux48 engine(seed());  // ���������������������
        uniform_int_distribution<> distrib(
            min, max);  // �����������Χ����Ϊ���ȷֲ�
        int random = distrib(engine);  // �����
        if (random > 50) {
            command.spawn(Health{.life = 100.0f}, Position{.x = 0, .y = 0});
        } else {
            command.spawn(Health{.life = 100.0f});
        }
    }
}

void print(ecs_trial::Command command) {
    auto view = command.registry().view<Health, Position>();
    for (auto [entity, health, pos] : view.each()) {
        std::cout << static_cast<int>(entity) << ": " << health.life
                  << std::endl;
    }
}

void print2(entt::entity&& entity, Health& health, Position& pos) {
    std::cout << static_cast<int>(entity) << ": " << health.life << std::endl;
}

int main() {
    ecs_trial::App app;
    app.command().spawn(Health{.life = 100.0f});
    app.run_system_command(start).run_system_command(print);
    app.run_system(print2);

    return 0;
}