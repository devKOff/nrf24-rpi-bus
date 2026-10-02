#include "radio_handler.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <iostream>
#include <chrono>

namespace pi_nrf24_link {

RadioHandler::RadioHandler()
    : radio_(CE_PIN, SPI_CS_PIN, SPI_SPEED_HZ)
    , chip_(nullptr)
    , irq_line_(nullptr)
    , ce_line_(nullptr)
    , rx_callback_(nullptr)
    , tx_callback_(nullptr)
    , initialized_(false)
    , listening_(false)
    , irq_thread_running_(false) {
}

RadioHandler::~RadioHandler() {
    shutdown();
}

bool RadioHandler::initialize(RxCallback rx_cb, TxCallback tx_cb) {
    if (initialized_.load()) {
        return true;
    }

    rx_callback_ = std::move(rx_cb);
    tx_callback_ = std::move(tx_cb);

    setup_gpio();

    if (!configure_radio()) {
        cleanup_gpio();
        return false;
    }

    radio_.begin();
    radio_.setChannel(RF_CHANNEL);
    radio_.setDataRate(RF24_250KBPS);
    radio_.setPALevel(RF24_PA_MAX);
    radio_.setRetries(RF_RETRY_DELAY, RF_RETRY_COUNT);
    radio_.setAddressWidth(ADDRESS_WIDTH);
    radio_.openWritingPipe(PIPE_ADDRESS);
    radio_.openReadingPipe(1, PIPE_ADDRESS);
    radio_.enableDynamicPayloads();
    radio_.enableAckPayload();
    radio_.setAutoAck(true);
    radio_.maskIRQ(1, 1, 0);

    initialized_.store(true);
    return true;
}

void RadioHandler::shutdown() {
    if (!initialized_.load()) {
        return;
    }

    stop_listening();
    radio_.powerDown();
    cleanup_gpio();
    initialized_.store(false);
}

void RadioHandler::setup_gpio() {
    chip_ = gpiod_chip_open_by_name("gpiochip0");
    if (!chip_) {
        chip_ = gpiod_chip_open_by_name("gpiochip4");
    }
    if (!chip_) {
        throw std::runtime_error("Failed to open GPIO chip");
    }

    irq_line_ = gpiod_chip_get_line(chip_, IRQ_PIN);
    if (!irq_line_) {
        throw std::runtime_error("Failed to get IRQ GPIO line");
    }

    ce_line_ = gpiod_chip_get_line(chip_, CE_PIN);
    if (!ce_line_) {
        throw std::runtime_error("Failed to get CE GPIO line");
    }

    struct gpiod_line_request_config irq_config = {
        .consumer = "nrf24-irq",
        .request_type = GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE,
        .flags = 0
    };

    if (gpiod_line_request(irq_line_, &irq_config) < 0) {
        throw std::runtime_error("Failed to request IRQ line for falling edge events");
    }

    struct gpiod_line_request_config ce_config = {
        .consumer = "nrf24-ce",
        .request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT,
        .flags = 0
    };

    if (gpiod_line_request(ce_line_, &ce_config) < 0) {
        throw std::runtime_error("Failed to request CE line as output");
    }

    gpiod_line_set_value(ce_line_, 0);
}

void RadioHandler::cleanup_gpio() {
    if (irq_line_) {
        gpiod_line_release(irq_line_);
        irq_line_ = nullptr;
    }
    if (ce_line_) {
        gpiod_line_set_value(ce_line_, 0);
        gpiod_line_release(ce_line_);
        ce_line_ = nullptr;
    }
    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }
}

bool RadioHandler::configure_radio() {
    if (!radio_.begin()) {
        return false;
    }
    return true;
}

bool RadioHandler::transmit(const RadioPacket& packet) {
    if (!initialized_.load() || listening_.load()) {
        if (tx_callback_) tx_callback_(false);
        return false;
    }

    radio_.stopListening();

    bool result = radio_.write(&packet, sizeof(RadioPacket));

    if (tx_callback_) {
        tx_callback_(result);
    }

    return result;
}

void RadioHandler::start_listening() {
    if (!initialized_.load() || listening_.load()) {
        return;
    }

    radio_.startListening();
    listening_.store(true);

    irq_thread_running_.store(true);
    irq_thread_ = std::thread(&RadioHandler::irq_handler_thread, this);
}

void RadioHandler::stop_listening() {
    if (!listening_.load()) {
        return;
    }

    irq_thread_running_.store(false);
    queue_cv_.notify_all();

    if (irq_thread_.joinable()) {
        irq_thread_.join();
    }

    radio_.stopListening();
    listening_.store(false);
}

void RadioHandler::irq_handler_thread() {
    int irq_fd = gpiod_line_event_get_fd(irq_line_);
    if (irq_fd < 0) {
        return;
    }

    struct pollfd pfd = {
        .fd = irq_fd,
        .events = POLLIN | POLLPRI,
        .revents = 0
    };

    while (irq_thread_running_.load()) {
        int ret = poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (pfd.revents & (POLLIN | POLLPRI)) {
            handle_irq();
        }
    }
}

void RadioHandler::handle_irq() {
    struct gpiod_line_event event;
    if (gpiod_line_event_read(irq_line_, &event) < 0) {
        return;
    }

    if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
        process_rx_fifo();
    }
}

void RadioHandler::process_rx_fifo() {
    uint8_t pipe_num = 0;
    while (radio_.available(&pipe_num)) {
        RadioPacket packet;
        uint8_t payload_size = radio_.getDynamicPayloadSize();

        if (payload_size != sizeof(RadioPacket)) {
            radio_.read(nullptr, payload_size);
            continue;
        }

        radio_.read(&packet, sizeof(RadioPacket));

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            rx_queue_.push(packet);
        }
        queue_cv_.notify_one();
    }

    radio_.flush_rx();
}

} // namespace pi_nrf24_link