#pragma once
// game.h - PROTOCOL.hex's rules and drawing, ported from the browser game (site/game.js).
#include "canvas.h"

namespace Game {
    enum Key { Up, Down, Left, Right, Rot, Burst, Trap, Cloak, Split, Pause, Mute };

    void init(const char* saveFolder);     // the best score etc. are kept there
    void newGame();
    bool next();                           // the "any key" after a cleared sector / game over / win; true: used up
    void key(Key k);
    void update(double dt);                // one fixed step (1/60 s)
    void draw(Canvas& c);
    bool paused();
    void setPaused(bool p);
    bool playing();                        // a sector is being played (not cleared / over / won)
    void load(int sector);                 // --sector N: straight into a sector (for testing; no high score)
    void shot();                           // --shot: the staged Scanner moment from the README
}
