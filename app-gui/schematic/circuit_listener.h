/**
 * @file    circuit_listener.h
 * @brief   What a Schematic tells whoever keeps data that a change of the circuit makes stale.
 */

#ifndef IMCSIM_CIRCUIT_LISTENER_H
#define IMCSIM_CIRCUIT_LISTENER_H

namespace GUI {

/**
 * @class   CircuitListener
 * @brief   Hears of the changes of a Schematic that make the results of its simulations stale.
 * @details The Schematic cannot know the results, which sit in a layer above it, so it calls the listener the moment
 *          the change is made, and the listener drops what no longer matches. Both methods run inside the call that
 *          changes the schematic, so no frame ever sees a stale result.
 */
class CircuitListener {
public:
    virtual ~CircuitListener() = default;

    /**
     * @brief   The circuit changed: an edit, a new document, an undo or a redo. Every result no longer matches it.
     */
    virtual void OnCircuitChanged() = 0;
    /**
     * @brief   A part was changed by hand in a live run, which keeps the transient the run goes on making.
     * @note    Every other result no longer matches the circuit and is dropped.
     */
    virtual void OnLiveChanged() = 0;
};

} // namespace GUI

#endif // IMCSIM_CIRCUIT_LISTENER_H
