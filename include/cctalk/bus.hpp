#pragma once

#include <type_traits>
#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <optional>
#include <boost/asio.hpp>

#if BOOST_VERSION < 106600
    namespace boost::asio {
        typedef io_service io_context;
    }
#endif

namespace cctalk {

    class Bus {
        typedef std::vector<unsigned char> Buffer;

        struct MessageHeader {
            unsigned char destination;
            unsigned char dataLength;
            unsigned char source;
            unsigned char headerCode;
        } __attribute__((packed));

        enum {CCTALK_SIZE_WITHOUT_DATA = 5};

    public:
        enum HeaderCode {
            RESET_DEVICE = 1,
            REQUEST_COIN_ID = 184,
            REQUEST_ACCEPT_COUNTER = 225,
            MODIFY_MASTER_INHIBIT_STATE = 228,
            READ_BUFFERED_CREDIT_OR_ERROR_CODES = 229,
            MODIFY_INHIBIT_STATUS = 231,
            PERFORM_SELF_TEST = 232,
            READ_INPUT_LINES = 237,
            REQUEST_SOFTWARE_VERSION = 241,
            REQUEST_SERIAL_NUMBER = 242,
            REQUEST_PRODUCT_CODE = 244,
            REQUEST_EQUIPMENT_CATEGORY_ID = 245,
            REQUEST_STATUS = 248,
            SIMPLE_POLL = 254
        };

        struct Command {
            unsigned char destination = 0;
            unsigned char source = 0;
            HeaderCode header = RESET_DEVICE;
        };

        struct DataCommand: Command {
            unsigned char *data = nullptr;
            unsigned char length = 0;
        };

        typedef std::function<void (std::optional<DataCommand> command)> CommandCallback;

        Bus(boost::asio::io_context &ioContext);
        virtual ~Bus() = default;

        bool open(const char *path);
        void close();

        bool ready();
        operator bool();

        void send(const Command command, std::function<void (bool)> callback = std::function<void (bool)>());
        void send(const DataCommand command, std::function<void (bool)> callback = std::function<void (bool)>());

        void receive(unsigned char destination, CommandCallback function);

    private:
        static Buffer createMessage(const Command &&command);
        static Buffer createMessage(const DataCommand &&command);
        static void addFrontMessage(Buffer &buffer,
                                    const Command &command,
                                    unsigned char dataLength = 0);
        static void addDataMessage(Buffer &buffer,
                                   const DataCommand &dataCommand);
        static void addChecksum(Buffer &buffer);


        void addReadCallback(std::pair<unsigned char, CommandCallback> callback);
        void startReading();
        void cancelReading();
        void processReceived();
        std::optional<CommandCallback> popCorrespondingReadCallback(const unsigned char destination);
        void readMissing(const unsigned char dataLength);
        void receivedAll();
        void callReadCallback();

        void write(Buffer &&buffer, std::function<void (bool)> &&callback);
        inline void configure();

        boost::asio::serial_port serialPort;

        std::mutex readCallbackMutex;
        std::atomic_bool isReading;
        std::vector<std::pair<unsigned char, CommandCallback>> readCallbacks;
        std::vector<unsigned char> readBuffer;
    };

}
