#include <cctalk/coinAcceptor.hpp>

#include <iostream>

namespace cctalk {

    CoinAcceptor::CoinAcceptor(cctalk::Bus &bus, unsigned char sourceAddress)
            : bus(bus), sourceAddress(sourceAddress) {
        validated = false;
    }

    void CoinAcceptor::open(unsigned char address, const std::function<void (bool)> callback) {
        validated = false;
        destinationAddress = address;

        initialize(std::move(callback));
    }

    bool CoinAcceptor::ready() {
        return validated;
    }

    CoinAcceptor::operator bool() {
        return ready();
    }

    void CoinAcceptor::disableCoin(const Coin &coin) {
        changedCoins = true;

        if (removeCoin(enabledCoins, coin)) {
            disabledCoins.push_back(coin);

        } else if (!coinExists(disabledCoins, coin)) {
            disabledCoins.push_back(coin);
        }
    }

    void CoinAcceptor::enableCoin(const Coin &coin) {
        changedCoins = true;

        if (removeCoin(disabledCoins, coin)) {
            enabledCoins.push_back(coin);

        } else if (!coinExists(enabledCoins, coin)) {
            enabledCoins.push_back(coin);
        }
    }

    bool CoinAcceptor::isWantedCoin(const Coin &coin) {
        int rating = 0;

        rating -= getCoinRating(disabledCoins, coin);
        rating += getCoinRating(enabledCoins, coin);

        return rating > 0;
    }

    void CoinAcceptor::start() {
        activateInhibitState([this] (bool success) {
            if (success) {
                changedCoins = false;
                enterPollLoop();
            }
        });
    }

    void CoinAcceptor::setCoinCallback(const std::function<void (const Coin &)> callback) {
        coinCallback = callback;
    }

    void CoinAcceptor::initialize(const std::function<void (bool)> &&callback) {
        validateEquipmentCategory(std::move(callback));
    }

    void CoinAcceptor::validateEquipmentCategory(std::function<void (bool)> callback) {
        auto command = createCommand(Bus::REQUEST_EQUIPMENT_CATEGORY_ID);
    
        if (!bus.ready()) {
            std::cout << "<CCTalk> Error: CoinAcceptor, serial bus is invalid" << std::endl;
            return callback(false);
        }
    
        bus.send(std::move(command));
    
        bus.receive(sourceAddress, [this, callback = std::move(callback)] (std::optional<Bus::DataCommand> command) mutable {
            if (!command) {
                std::cout << "<CCTalk> Error: CoinAcceptor, validateEquipmentCategory failed! No response received." << std::endl;
                return callback(false);
            }
    
            if (command->length == 0) {
                std::cout << "<CCTalk> Error: CoinAcceptor, received empty response for equipment category!" << std::endl;
                return callback(false);
            }
    
            std::string_view equipmentCategory(reinterpret_cast<char*>(command->data), command->length);
            std::cout << "<CCTalk> Received equipment category: [" << equipmentCategory << "]" << std::endl;
    
            if (equipmentCategory == "Coin Acceptor") {
                std::cout << "<CCTalk> initializeSupportedCoins" << std::endl;
                return initializeSupportedCoins(std::move(callback));
            }
    
            std::cout << "<CCTalk> Error: CoinAcceptor, received invalid equipment category: [" << equipmentCategory << "]" << std::endl;
            return callback(false);
        });
    }

    void CoinAcceptor::initializeSupportedCoins(const std::function<void (bool)> &&callback) {
        supportedCoins.clear();
        supportedCoins.reserve(16);

        addLeftSupportedCoins(std::move(callback));
    }


    void CoinAcceptor::addLeftSupportedCoins(const std::function<void (bool)> &&callback) {
        unsigned char coinId = supportedCoins.size() + 1;

        auto command = createDataCommand(Bus::REQUEST_COIN_ID, &coinId, 1);

        bus.send(command);

        bus.receive(sourceAddress, [this, callback] (std::optional<Bus::DataCommand> command) {
            if (command) {
                std::string_view coinCode(reinterpret_cast<char*>(command->data), command->length);

                if (addSupportedCoin(coinCode)) {
                    addLeftSupportedCoins(std::move(callback));
                } else {
                    initializeCounter(std::move(callback));
                }
            } else {
                std::cout << "<CCTalk> Error: CoinAcceptor, addLeftSupportedCoins failed!" << std::endl;
                callback(false);
            }
        });
    }

    bool CoinAcceptor::addSupportedCoin(const std::string_view &coinCode) {
        if (coinCode.size() != 6) {
            return false;
        }

        if (auto currency = Coin::makeCurrency(coinCode.data())) {
            if (auto value = Coin::makeValue(coinCode.data() + 2)) {
                if (auto revision = Coin::makeRevision(coinCode[5])) {
                    supportedCoins.emplace_back(currency, value, revision);
                    std::cout << "<CCTalk> added " << supportedCoins.back() << " with id " << supportedCoins.size() << std::endl;
                    return true;
                }
            }
        }
        return false;
    }

    void CoinAcceptor::initializeCounter(const std::function<void (bool)> &&callback) {
        auto command = createCommand(Bus::READ_BUFFERED_CREDIT_OR_ERROR_CODES);

        bus.send(command);
        bus.receive(sourceAddress, [this, callback] (std::optional<Bus::DataCommand> command) {
            if (command && command->length > 0) {
                lastCounter = command->data[0];
                validated = true;
                callback(true);
            } else {
                std::cout << "<CCTalk> Error: CoinAcceptor, initializeCounter failed!" << std::endl;
                callback(false);
            }
        });
    }

    Bus::Command CoinAcceptor::createCommand(const Bus::HeaderCode code) {
        Bus::Command command;
        command.source = sourceAddress;
        command.destination = destinationAddress;
        command.header = code;
        return std::move(command);
    }

    Bus::DataCommand CoinAcceptor::createDataCommand(const Bus::HeaderCode code,
                                                       unsigned char *data,
                                                       unsigned char length) {
        Bus::DataCommand command;
        command.source = sourceAddress;
        command.destination = destinationAddress;
        command.header = code;
        command.data = data;
        command.length = length;
        return std::move(command);
    }

    bool CoinAcceptor::removeCoin(std::vector<Coin> &vector, const Coin &coin) {
        auto iterator = std::find(vector.begin(), vector.end(), coin);
        if (iterator != vector.end()) {
            vector.erase(iterator);
            return true;
        }
        return false;
    }

    bool CoinAcceptor::coinExists(std::vector<Coin> &vector, const Coin &coin) {
        auto iterator = std::find(vector.begin(), vector.end(), coin);
        return iterator != vector.end();
    }

    int CoinAcceptor::getCoinRating(std::vector<Coin> &rater, const Coin &coin) {
        int rating = 0;
        for (auto &raterCoin: rater) {
            if (raterCoin.sameType(coin)) {
                rating += raterCoin.getSpecialization();
            }
        }

        return rating;
    }

    void CoinAcceptor::activateInhibitState(std::function<void (bool)> callback) {
        auto inhibitState = generateInhibitState();

        setInhibitState(std::move(inhibitState), [this, callback] (bool success) {
            if (success) {
                setMasterInhibitState(true, std::move(callback));
            } else {
                std::cout << "<CCTalk> Error: CoinAcceptor, activateInhibitState failed!" << std::endl;
                callback(false);
            }
        });
    }

    void CoinAcceptor::setMasterInhibitState(bool value, std::function<void (bool)> callback) {
        unsigned char data = value;
        auto command = createDataCommand(Bus::MODIFY_MASTER_INHIBIT_STATE, &data, 1);
        bus.send(std::move(command));
        bus.receive(sourceAddress, [callback] (std::optional<Bus::DataCommand> response) {
            callback(response.has_value());
        });
    }

    void CoinAcceptor::setInhibitState(std::vector<unsigned char> state, std::function<void (bool)> callback) {
        auto command = createDataCommand(Bus::MODIFY_INHIBIT_STATUS, state.data(), state.size());
        bus.send(std::move(command));
        bus.receive(sourceAddress, [callback] (std::optional<Bus::DataCommand> response) {
            callback(response.has_value());
        });
    }

    std::vector<unsigned char> CoinAcceptor::generateInhibitState() {
        std::size_t stateBytes = (supportedCoins.size() >> 3) + ((supportedCoins.size() & 0b111) > 0);

        std::vector<unsigned char> inhibitState(stateBytes);
        std::memset(inhibitState.data(), 0, inhibitState.size());

        for (int coinNo = 0; coinNo < supportedCoins.size(); coinNo++) {
            std::cout << "<CCTalk> checking: " << supportedCoins[coinNo] << std::endl;
            if (isWantedCoin(supportedCoins[coinNo])) {
                std::cout << "<CCTalk> " << supportedCoins[coinNo] << " is Wanted coin" << std::endl;
                inhibitState[coinNo >> 3] |= 1 << ((coinNo) & 0b111);
            }
        }

        return std::move(inhibitState);
    }

    void CoinAcceptor::enterPollLoop() {
        bus.send(createCommand(Bus::READ_BUFFERED_CREDIT_OR_ERROR_CODES));
        bus.receive(sourceAddress, std::bind(&CoinAcceptor::handlePollLoop, this, std::placeholders::_1));
    }

    void CoinAcceptor::handlePollLoop(std::optional<Bus::DataCommand> command) {
        if (command && command->length >= 3) {
            if (command->data[0] != lastCounter) {
                lastCounter = command->data[0];
                unsigned char lastCoinId = command->data[1];
                if (lastCoinId > 0 && lastCoinId <= supportedCoins.size()) {
                    if (coinCallback) {
                        coinCallback(supportedCoins[lastCoinId - 1]);
                    }
                    std::cout << "<CCTalk> got " << supportedCoins[lastCoinId - 1] << std::endl;
                } else {
                    std::cout << "<CCTalk> got invalid coin" << std::endl;
                }
            }

            if (changedCoins) {
                setInhibitState(generateInhibitState(), [this] (bool success) {
                    if (success) {
                        enterPollLoop();
                    }
                });
            } else {
                enterPollLoop();
            }
        }
    }

    void CoinAcceptor::updateCurrency(const char* newCurrency) {
        // Disable all coins
        for (const auto& coin : enabledCoins) {
            disableCoin(coin);
        }
    
        enabledCoins.clear();
    
        // Fetch new supported coins from the device
        addAllSupportedCoins([this](bool success) {
            if (!success) {
                std::cout << "<CCTalk> Error: Could not fetch supported coins" << std::endl;
                return;
            }
    
            // Enable the newly fetched coins
            for (const auto& coin : supportedCoins) {
                cctalk::Coin coinToEnable(coin.getCurrency(), coin.getValue());
                enableCoin(coinToEnable);
            }
    
            // Update inhibit state
            setInhibitState(generateInhibitState(), [](bool success) {
                if (success) {
                    std::cout << "<CCTalk> Currency updated successfully" << std::endl;
                } else {
                    std::cout << "<CCTalk> Error updating currency" << std::endl;
                }
            });
        });
    }

    void CoinAcceptor::addAllSupportedCoins(const std::function<void(bool)> &&callback) {
        unsigned char coinId = 1;
        supportedCoins.clear();
    
        // save function, to prepare resursive call
        std::function<void()> fetchNextCoin = [&]() {
            auto command = createDataCommand(Bus::REQUEST_COIN_ID, &coinId, 1);
    
            bus.send(command);
            bus.receive(sourceAddress, [this, &coinId, &fetchNextCoin, callback](std::optional<Bus::DataCommand> command) mutable {
                if (command) {
                    std::string_view coinCode(reinterpret_cast<char*>(command->data), command->length);
                    if (addSupportedCoin(coinCode)) {
                        std::cout << "<CCTalk> Successfully added coin with id: " << coinId << std::endl;
                        coinId++;
                        fetchNextCoin(); // call recursive
                    } else {
                        std::cout << "<CCTalk> No more coins or invalid coin data." << std::endl;
                        callback(true);
                    }
                } else {
                    std::cout << "<CCTalk> Error: Failed to receive coin data for id: " << coinId << std::endl;
                    callback(false);
                }
            });
        };
    
        // start call
        fetchNextCoin();
    }   
}
