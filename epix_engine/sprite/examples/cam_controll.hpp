#pragma once

struct CamControllPlugin {
    void build(core::App& app) {
        app.add_systems(
            core::Update,
            core::into([](core::Query<core::Item<const render::camera::Camera&, render::camera::Projection&,
                                                 transform::Transform&>> camera,
                          core::EventReader<input::MouseScroll> scroll_input,
                          core::Res<input::ButtonInput<input::KeyCode>> key_states) {
                if (auto opt = camera.single(); opt.has_value()) {
                    auto&& [cam, proj, trans] = *opt;
                    if (key_states->pressed(input::KeyCode::KeySpace)) {
                        trans.translation = glm::vec3(0, 0, 0);
                        proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
                            *ortho = render::camera::OrthographicProjection{};
                            return true;
                        });
                        return;
                    }
                    glm::vec3 delta(0.0f);
                    if (key_states->pressed(input::KeyCode::KeyW)) {
                        delta += glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyS)) {
                        delta -= glm::vec3(0, 0.1f, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyA)) {
                        delta -= glm::vec3(0.1f, 0, 0);
                    }
                    if (key_states->pressed(input::KeyCode::KeyD)) {
                        delta += glm::vec3(0.1f, 0, 0);
                    }
                    if (glm::length(delta) > 0.0f) {
                        delta = glm::normalize(delta) * 0.1f;
                        trans.translation += delta;
                    }
                    proj.as_orthographic().transform([&](render::camera::OrthographicProjection* ortho) {
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