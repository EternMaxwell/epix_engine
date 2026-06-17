#pragma once

#include <epix/common.hpp>

#ifndef EPIX_CXX_MODULE
#include <glm/glm.hpp>
#endif

namespace epix::ext::fallingsand {

/**
 * @brief Per-cell air data stored in a PackedLayer within each chunk.
 *
 * Every cell has air properties regardless of particle occupancy.
 * When a particle occupies a cell, the air cell is dormant (skipped
 * during air simulation) but still holds values for heat exchange.
 */
EPIX_EXPORT struct AirCell {
    float temperature = 293.0f;  ///< Kelvin (20 °C).
    float density     = 1.225f;  ///< kg/m³ at sea level.
    glm::vec2 velocity{};        ///< Air flow velocity in cells/s.
};

/**
 * @brief Per-cell thermal data for the element layer.
 *
 * Lives in its own dense layer alongside Element so that heat-only updates
 * (conduction / convection / staging) do not mark the element grid modified.
 *
 * When an Element is moved or swapped between cells, its ThermalCell must
 * travel with it so the bookkeeping stays consistent.
 */
EPIX_EXPORT struct ThermalCell {
    float temperature  = 293.0f;  ///< Current temperature in Kelvin.
    float staging_heat = 0.0f;    ///< Accumulated latent heat (J) for phase transitions.
    float heat_emitted = 0.0f;    ///< Total heat (J) emitted while burning.
};

/** @brief Specific heat of air at constant pressure, J/(kg·K). */
EPIX_EXPORT constexpr float kAirSpecificHeat = 1005.0f;

/** @brief Thermal conductivity of air, W/(m·K). */
EPIX_EXPORT constexpr float kAirThermalConductivity = 0.026f;

}  // namespace epix::ext::fallingsand
