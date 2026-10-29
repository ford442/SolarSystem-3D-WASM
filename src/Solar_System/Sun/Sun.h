#ifndef SOLARSYSTEM_SUN_H
#define SOLARSYSTEM_SUN_H
#include "../Star.h"

class Sun : public Star {
public:
    explicit Sun(StarInfo&& starInfo);
    void TakeStarSystemCenter() override;
    float SizeFactor() const override;
    float GetSceneRadius() const override;
};


#endif //SOLARSYSTEM_SUN_H
