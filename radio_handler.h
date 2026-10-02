#ifndef RADIO_HANDLER_H
#define RADIO_HANDLER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <gpiod.h>
#include <RF24/RF24.h>
#include "config.h"

namespace pi_nrf24_link {

class RadioHandler {
public:
    using RxCallback = std::function<void(const RadioPacket&)>;
    using TxCallback = std::function<void(bool)>;

    RadioHandler();
    ~RadioHandler();

    RadioHandler(const RadioHandler&) = delete;
    RadioHandler& operator=(const RadioHandler&) = delete;
    RadioHandler(RadioHandler&&) = delete;
    RadioHandler& operator=(RadioHandler&&) = delete;

    bool initialize(RxCallback rx_cb, TxCallback tx_cb);
    void shutdown();

    bool transmit(const RadioPacket& packet);
    bool is_initialized() const { return initialized_.load(); }
    bool is_listening() const { return listening_.load(); }

    void start_listening();
    void stop_listening();

private:
    RF24 radio_;
    gpiod_chip* chip_;
    gpiod_line* irq_line_;
    gpiod_line* ce_line_;

    RxCallback rx_callback_;
    TxCallback tx_callback_;

    std::atomic<bool> initialized_;
    std::atomic<bool> listening_;
    std::atomic<bool> irq_thread_running_;
    std::thread irq_thread_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<RadioPacket> rx_queue_;

    void irq_handler_thread();
    void process_rx_fifo();
    void handle_irq();
    void setup_gpio();
    void cleanup_gpio();
    bool configure_radio();
};

} // namespace pi_nrf24_link

#endif // RADIO_HANDLER_H