#include "epix/world/sand.h"

using namespace epix::world::sand;

EPIX_API bool Particle::freefall() const { return bitfield & FREEFALL; }
EPIX_API void Particle::set_freefall(bool freefall) {
    if (freefall) {
        bitfield |= FREEFALL;
    } else {
        bitfield &= ~FREEFALL;
    }
}
EPIX_API bool Particle::updated() const { return bitfield & UPDATED; }
EPIX_API void Particle::set_updated(bool updated) {
    if (updated) {
        bitfield |= UPDATED;
    } else {
        bitfield &= ~UPDATED;
    }
}

EPIX_API bool Particle::valid() const { return elem_id != -1; }
EPIX_API Particle::operator bool() const { return valid(); }
EPIX_API bool Particle::operator!() const { return !valid(); }

EPIX_API PartDef::PartDef(const std::string& name) : id(name) {}
EPIX_API PartDef::PartDef(int id) : id(id) {}