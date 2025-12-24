#pragma once

#include <epix/input.hpp>
#include <epix/render.hpp>

#include "glm/geometric.hpp"

struct CamControllPlugin {
    void build(epix::App& app) {
        app.add_systems(
            epix::Update,
            epix::into([](epix::Query<epix::Item<const epix::render::camera::Camera&, epix::render::camera::Projection&,
                                                 epix::transform::Transform&>> camera,
                          epix::EventReader<epix::input::MouseScroll> scroll_input,
                          epix::Res<epix::input::ButtonInput<epix::input::KeyCode>> key_states) {
                if (auto opt = camera.single(); opt.has_value()) {
                    auto&& [cam, proj, trans] = *opt;
                    if (key_states->pressed(epix::input::KeyCode::KeySpace)) {
                        trans.translation = glm::vec3(0, 0, 0);
                        proj.as_orthographic().transform([&](epix::render::camera::OrthographicProjection* ortho) {
                            *ortho = epix::render::camera::OrthographicProjection{};
                            return true;
                        });
                        return;
                    }
                    glm::vec3 delta(0.0f);
                    if (key_states->pressed(epix::input::KeyCode::KeyW)) {
                        delta += glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyS)) {
                        delta -= glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyA)) {
                        delta -= glm::vec3(0.1f, 0, 0);
                    }
                    if (key_states->pressed(epix::input::KeyCode::KeyD)) {
                        delta += glm::vec3(0.1f, 0, 0);
                    }
                    if (glm::length(delta) > 0.0f) {
                        delta = glm::normalize(delta) * 0.1f;
                        trans.translation += delta;
                    }
                    proj.as_orthographic().transform([&](epix::render::camera::OrthographicProjection* ortho) {
                        for (const auto& e : scroll_input.read()) {
                            float scale = std::exp(-static_cast<float>(e.yoffset) * 0.1f);
                            ortho->scale *= scale;
                        }
                        // Key space reset
                        return true;
                    });
                }
            }).set_name("camera control"));
    }
};