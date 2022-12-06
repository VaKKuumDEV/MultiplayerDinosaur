#define _WINSOCK_DEPRECATED_NO_WARININGS
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <random>
#include <ctime>
#include <queue>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

using namespace std;

const int TPS = 60;
const int SKIPPING_FRAMES = 0;
const int FLOOR_HEIGHT = 2;
std::pair<int, int> sizes;

void ClearScreen();
void loopLogic();
void keyControl();
void acceptServerConnections();
void waitForConnections();
void serverModeReceive();
void serverModeSend();
void waitForStart();
void clientModeReceive();
void clientModeSend();
bool isGamePlaying();
std::pair<int, int> getConsoleSize();
vector<string> split(string str, string delimiter);
string randomString(const int length);

enum PlayingModes {
    NONE,
    SINGLE,
    SERVER,
    CLIENT,
};

class GameObject {
public:
    struct Position {
    public:
        int X = 0;
        int Y = 0;
    };
    enum Types {
        PLAYER,
        ENEMY,
        DECOR,
    };
    using Matrix = std::vector<std::vector<char>>;

    GameObject(Types type) {
        Type = type;
    }
    Types getType() {
        return Type;
    }
    void setInLive(bool value) {
        isLiving = value;
    }
    bool isInLive() {
        return isLiving;
    }
    bool isCollisingWith(GameObject &obj) {
        int objMinX = obj.getPosition().X;
        int objMaxX = obj.getPosition().X + obj.getWidth();
        int objMinY = obj.getPosition().Y;
        int objMaxY = obj.getPosition().Y + obj.getHeight();

        bool collisionX = false;
        bool collisionY = false;
        if ((getPosition().X <= objMinX && (getPosition().X + getWidth()) >= objMaxX) || (objMinX <= getPosition().X && objMaxX >= getPosition().X)) collisionX = true;
        if ((getPosition().Y <= objMinY && (getPosition().Y + getHeight()) >= objMaxY) || (objMinY <= getPosition().Y && objMaxY >= getPosition().Y)) collisionY = true;
        return (collisionX && collisionY);
    }

protected:
    virtual Matrix getMatrix() = 0;
    virtual Position getPosition() = 0;
    virtual int getHeight() = 0;
    virtual int getWidth() = 0;

private:
    Types Type;
    bool isLiving = true;
    GameObject() = delete;
};

class PlayerObject : public GameObject {
public:
    PlayerObject() :GameObject(PLAYER) {}
    Matrix getMatrix() {
        Matrix matrix = std::vector<std::vector<char>>(height, std::vector<char>(width, '#'));
        return matrix;
    }
    bool isOnGround() {
        return !isJumping;
    }
    void processJump() {
        if (isJumping) {
            if (moveSkipFrames <= 0) {
                jumpVelocity += jumpKoef;
                int jumpHeight = height * 2;

                if (jumpVelocity <= 0) {
                    isJumping = false;
                    jumpVelocity = 0;
                    jumpKoef = 1;
                }
                else if (jumpVelocity >= jumpHeight) {
                    jumpKoef = -1;
                    moveSkipFrames = SKIPPING_FRAMES;
                }
                else if (jumpVelocity < jumpHeight) {
                    moveSkipFrames = SKIPPING_FRAMES;
                }
            }
            else  moveSkipFrames--;
        }
    }
    int getJumpVelocity() {
        return jumpVelocity;
    }
    void jump() {
        if (!isJumping) {
            isJumping = true;
            moveSkipFrames = 0;
        }
    }
    bool isInLive() {
        return GameObject::isInLive();
    }
    int getWidth() {
        return width;
    }
    int getHeight() {
        return height;
    }
    Position getPosition() {
        int coordX = min(sizes.first, 5);
        int coordY = sizes.second - height - FLOOR_HEIGHT - 1;

        if (!isOnGround()) {
            coordY -= getJumpVelocity();
        }
        Position pos{ coordX, coordY };
        return pos;
    }

private:
    bool isJumping = false;
    int jumpVelocity = 0;
    int moveSkipFrames = 0;
    int jumpKoef = 1;
    int height = 3;
    int width = 3;
};

class PregradaObject :GameObject {
public:
    PregradaObject(int w, int h) :GameObject(ENEMY) {
        if (h < 1) h = 1;
        if (w < 1) w = 1;
        width = w;
        height = h;
    }
    Matrix getMatrix() {
        Matrix matrix = std::vector<std::vector<char>>(height, std::vector<char>(width, '#'));
        return matrix;
    }
    int getWidth() {
        return width;
    }
    int getHeight() {
        return height;
    }
    int getWidthOffset() {
        return widthOffset;
    }
    void processEnemy() {
        if (moveSkipFrames <= 0) {
            if (widthOffset <= 0) setInLive(false);
            else {
                widthOffset--;
                moveSkipFrames = SKIPPING_FRAMES;
            }
        }
        else moveSkipFrames--;
    }
    bool isInLive() {
        return GameObject::isInLive();
    }
    Position getPosition() {
        int enemyX = getWidthOffset() - getWidth();
        int enemyY = sizes.second - getHeight() - FLOOR_HEIGHT - 1;

        Position pos{ enemyX, enemyY };
        return pos;
    }
    bool isCollisingWith(GameObject &obj) {
        return GameObject::isCollisingWith(obj);
    }

private:
    int width;
    int height;
    int widthOffset = 150;
    int moveSkipFrames = SKIPPING_FRAMES;
};

struct Score {
public:
    string nickname = "";
    int score = 0;
    bool inLive = false;
};

class Client {
private:
    vector<Score> scores = vector<Score>();
    int myScore = 0;
    string nickname = "";
    bool inLive = true;
    bool inited = false;
    bool serverInited = false;
    bool gameStarted = false;
    bool serverConnectionCompleted = false;
    bool serverClosed = false;
    int startTicks = 0;
    SOCKET clientSocket;
    string recBuffer = "";

    bool sendingData = false;
    queue<string> sendBuffer = queue<string>();

public:
    Client() = delete;
    Client(string nick) {
        nickname = nick;
    }
    void connectTo(string ip, int port) {
        WSADATA wsaData;
        clientSocket = INVALID_SOCKET;

        struct addrinfo *result = NULL,
            *ptr = NULL,
            hints{};
        int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaResult != 0) {
            cout << "Error on init WSA: " << wsaResult << endl;
            return;
        }

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        int addrResult = getaddrinfo("127.0.0.1", "5555", &hints, &result);
        if (addrResult != 0) {
            cout << "Error on getting address: " << addrResult << endl;
            WSACleanup();
            return;
        }

        for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
            clientSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

            if (clientSocket == INVALID_SOCKET) {
                cout << "Can`t create socket: " << WSAGetLastError() << endl;
                WSACleanup();
                return;
            }

            int connectResult = connect(clientSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
            if (connectResult == SOCKET_ERROR) {
                closesocket(clientSocket);
                clientSocket = INVALID_SOCKET;
                cout << "Server connection failed: " << WSAGetLastError() << endl;
                return;
            }
        }

        freeaddrinfo(result);
        if (clientSocket == INVALID_SOCKET) {
            cout << "Error on connectiong server" << endl;
            WSACleanup();
            return;
        }

        u_long iMode = 1;
        int modeResult = ioctlsocket(clientSocket, FIONBIO, &iMode);
        if (modeResult == SOCKET_ERROR) {
            cout << "Error on setting connection mode: " << modeResult << endl;
            closesocket(clientSocket);
            WSACleanup();
            return;
        }

        char value = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
        inited = true;
    }
    bool isStarted() {
        return gameStarted;
    }
    int getStartTicks() {
        return startTicks;
    }
    void setNickname(string nick) {
        nickname = nick;
    }
    string getNickname() {
        return nickname;
    }
    bool isInited() {
        return inited;
    }
    bool isServerInited() {
        return serverInited;
    }
    bool isInLive() {
        return inLive;
    }
    void setInLive(bool val) {
        inLive = val;
    }
    int getMyScore() {
        return myScore;
    }
    void setMyScore(int val) {
        myScore = val;
    }
    void addInBuffer(string val) {
        sendBuffer.push(val);
    }
    void popBuffer() {
        sendBuffer.pop();
    }
    string frontBuffer() {
        return sendBuffer.front();
    }
    bool hasBufferData() {
        return sendBuffer.size() > 0;
    }
    bool isSendingData() {
        return sendingData;
    }
    void setSendingData(bool val) {
        sendingData = val;
    }
    vector<Score> getScores() {
        return scores;
    }
    bool isAllCompleted() {
        if (serverClosed) return true;
        bool completed = true;
        for (auto& sc : scores) {
            if (sc.inLive) completed = false;
        }

        return !isInLive() && completed;
    }
    void stop() {
        gameStarted = false;
        closesocket(clientSocket);
        WSACleanup();
    }

    void process() {
        if (recBuffer.size() > 0) {
            vector<string> bufferSplits = split(recBuffer, "&");
            recBuffer = "";

            for (string bufferSplit : bufferSplits) {
                if (bufferSplit.size() == 0) continue;

                vector<string> splitSplits = split(bufferSplit, ";");
                if (splitSplits.size() > 0) {
                    string status = splitSplits[0];
                    if (status == "ok" && splitSplits.size() > 1) {
                        string mode = splitSplits[1];
                        //доделать обработку
                        if (mode == "started") {
                            gameStarted = true;
                        }
                        else if (mode == "closed") {
                            stop();
                            serverClosed = true;
                            return;
                        }
                        else {
                            vector<string> splitSplitSplits = split(mode, ":");
                            if (splitSplitSplits.size() == 2) {
                                string modeMode = splitSplitSplits[0];
                                if (modeMode == "wait") {
                                    int ticksLeft = stoi(splitSplitSplits[1]);
                                    startTicks = ticksLeft;
                                }
                            }
                        }

                        scores.clear();
                        for (int i = 2; i < splitSplits.size(); i++) {
                            vector<string> playerData = split(splitSplits[i], ":");
                            if (playerData.size() == 3) {
                                string playerNick = playerData[0];
                                int playerScore = stoi(playerData[1]);
                                bool playerInLive = playerData[2] == "1";
                                Score playerS = { playerNick, playerScore, playerInLive };
                                scores.push_back(playerS);
                            }
                        }

                        if (!serverConnectionCompleted && scores.size() > 0) {
                            cout << "Connected to " << scores[scores.size() - 1].nickname << "'s server" << endl;
                            serverConnectionCompleted = true;
                        }
                    }
                }
            }
        }

        if (!serverClosed) {
            if (!isServerInited()) {
                string sendString = "init;" + getNickname();
                addInBuffer(sendString);
                serverInited = true;
            }
            else if (!isInLive()) {
                string sendString = "died;" + to_string(getMyScore());
                addInBuffer(sendString);
            }
            else {
                string sendString = "playing;" + to_string(getMyScore());
                addInBuffer(sendString);
            }
        }
    }

    void clientReceive() {
        char buffer[1024] = { 0 };

        string receivedString = "";
        int receivedBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (receivedBytes != SOCKET_ERROR) receivedString = string(buffer);

        memset(buffer, 0, sizeof(buffer));
        recBuffer += receivedString;
    }

    void clientSend() {
        if (!isSendingData() && hasBufferData() && !serverClosed) {
            setSendingData(true);
            sendToServer(frontBuffer());
            popBuffer();

            setSendingData(false);
        }
    }

    void sendToServer(string message) {
        int sentBytes = send(clientSocket, message.c_str(), (int)strlen(message.c_str()), 0);
        if (sentBytes == SOCKET_ERROR) cout << "Error on sending data to socket: " << WSAGetLastError() << endl;
    }
};

class ServClient {
private:
    int score = 0;
    SOCKET socket;
    std::string nickname;
    bool inited = false;
    bool inLive = true;
    bool lastQueryOkay = true;
    bool sendingData = false;
    bool serverWorking = true;
    queue<string> sendBuffer = queue<string>();

public:
    ServClient() = default;
    ServClient(SOCKET soc) {
        socket = soc;
    }
    SOCKET* getSocket() {
        return &socket;
    }
    int getScore() {
        return score;
    }
    void setScore(int val) {
        score = val;
    }
    string getNickname() {
        return nickname;
    }
    void setNickname(string val) {
        nickname = val;
    }
    bool isInited() {
        return inited;
    }
    void setInited() {
        inited = true;
    }
    bool isInLive() {
        return inLive;
    }
    void setDied() {
        inLive = false;
    }
    bool getLastQuery() {
        return lastQueryOkay;
    }
    void setLastQuery(bool value) {
        lastQueryOkay = value;
    }
    void addInBuffer(string val) {
        if (!isServerWorking()) return;
        sendBuffer.push(val);
    }
    void popBuffer() {
        sendBuffer.pop();
    }
    string frontBuffer() {
        return sendBuffer.front();
    }
    bool hasBufferData() {
        return sendBuffer.size() > 0;
    }
    bool isSendingData() {
        return sendingData;
    }
    void setSendingData(bool val) {
        sendingData = val;
    }
    bool isServerWorking() {
        return serverWorking;
    }
    void setServerWorking(bool val) {
        serverWorking = val;
    }
};

class Server {
private:
    const int START_TICKS = TPS * 10;
    map<string, ServClient> clients = map<string, ServClient>();
    SOCKET serverSocket = INVALID_SOCKET;
    bool inited = false;
    bool started = false;
    bool working = true;
    int startTicks = START_TICKS;
    string nickname;
    int myScore = 0;
    bool myPlaying = false;
    bool isStopping = false;

    bool sendToClient(ServClient client, string message) {
        message += "&";

        int sentBytes = send(*client.getSocket(), message.c_str(), (int)strlen(message.c_str()), 0);
        if (sentBytes == SOCKET_ERROR) return false;
        return true;
    }

public:
    Server(string nick) {
        nickname = nick;
        WSADATA wsaData;

        struct addrinfo* result = NULL;
        struct addrinfo hints;

        int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaResult != 0) {
            cout << "Error on init WSA: " << WSAGetLastError() << endl;
            return;
        }

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;

        int addrResult = getaddrinfo(NULL, "5555", &hints, &result);
        if (addrResult != 0) {
            cout << "Error on get address: " << WSAGetLastError() << endl;
            WSACleanup();
            return;
        }

        serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (serverSocket == INVALID_SOCKET) {
            cout << "Error on creationg socket: " << WSAGetLastError() << endl;
            freeaddrinfo(result);
            WSACleanup();
            return;
        }

        u_long iMode = 1;
        int modeResult = ioctlsocket(serverSocket, FIONBIO, &iMode);
        if (modeResult == SOCKET_ERROR) {
            cout << "Error on setting mode: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        int bindResult = bind(serverSocket, result->ai_addr, (int)result->ai_addrlen);
        if (bindResult == SOCKET_ERROR) {
            cout << "Bind failed: " << WSAGetLastError() << endl;
            freeaddrinfo(result);
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        freeaddrinfo(result);
        int listenResult = listen(serverSocket, SOMAXCONN);
        if (listenResult == SOCKET_ERROR) {
            cout << "Error on start listening: " << WSAGetLastError() << endl;
            closesocket(serverSocket);
            WSACleanup();
            return;
        }

        inited = true;
    }
    void addClient(SOCKET socket) {
        string identifier = randomString(10);
        ServClient client(socket);

        clients[identifier] = client;
    }
    vector<Score> getClients() {
        vector<Score> cls = vector<Score>();
        for (auto& cl : clients) {
            Score score{ cl.second.getNickname(), cl.second.getScore(), cl.second.isInLive() };
            cls.push_back(score);
        }

        return cls;
    }
    void stop() {
        started = false;
        for (auto& client : clients) closesocket(*client.second.getSocket());
        closesocket(serverSocket);
        WSACleanup();
        working = false;
    }
    void serverProcess() {
        std::vector<std::string> ids = std::vector<std::string>();
        for (auto& cl : clients) {
            ids.push_back(cl.first);
        }

        bool isAllCompleted = true;
        for (int keyIndex = 0; keyIndex < ids.size(); keyIndex++) {
            ServClient* client = &clients[ids[keyIndex]];
            if (client->isInLive()) isAllCompleted = false;

            if (client->getLastQuery()) {
                vector<Score> scores = vector<Score>();
                for (int keyIndex2 = 0; keyIndex2 < ids.size(); keyIndex2++) {
                    if (keyIndex2 != keyIndex) {
                        Score score{ clients[ids[keyIndex2]].getNickname(), clients[ids[keyIndex2]].getScore(), clients[ids[keyIndex2]].isInLive() };
                        scores.push_back(score);
                    }
                }

                string clientMessage = "ok;";
                if (started) clientMessage += "started;";
                else clientMessage += "wait:" + to_string(startTicks) + ";";

                for (auto& sc : scores) {
                    string m = sc.nickname + ":" + to_string(sc.score) + ":" + (sc.inLive ? "1" : "0");
                    clientMessage += m + ";";
                }

                string myScoreStr = nickname + ":" + to_string(myScore) + ":" + (myPlaying ? "1" : "0");
                clientMessage += myScoreStr;

                client->addInBuffer(clientMessage);
            }
        }

        if (!isPlaying() && isAllCompleted && !isStopping) {
            string stopMessage = "ok;closed";
            for (int keyIndex = 0; keyIndex < ids.size(); keyIndex++) {
                ServClient* client = &clients[ids[keyIndex]];

                client->addInBuffer(stopMessage);
                client->setServerWorking(false);
            }

            isStopping = true;
        }
    }
    void serverReceive() {
        if (isStopping) return;

        std::vector<std::string> ids = std::vector<std::string>();
        for (auto& cl : clients) {
            ids.push_back(cl.first);
        }

        char buffer[1024] = { 0 };
        for (int keyIndex = 0; keyIndex < ids.size(); keyIndex++) {
            string identificator = ids[keyIndex];

            ServClient* client = &clients[identificator];
            string receivedString = "";
            bool hasError = false;

            if (!client->isInLive()) continue;
            SOCKET* clientSocket = client->getSocket();

            if (recv(*clientSocket, buffer, sizeof(buffer), 0) == SOCKET_ERROR) {
                //ошибка приема сокета
                hasError = true;
            }
            receivedString = string(buffer);
            memset(buffer, 0, sizeof(buffer));

            if (!hasError) {
                vector<string> receivedPieces = split(receivedString, ";");
                bool isOkay = false;

                if (receivedPieces.size() >= 1) {
                    string playerStatus = receivedPieces[0];
                    if (playerStatus == "playing") {
                        if (receivedPieces.size() >= 2) {
                            int playerScore = stoi(receivedPieces[1]);
                            client->setScore(playerScore);
                            isOkay = true;
                        }
                    }
                    else if (playerStatus == "init") {
                        if (receivedPieces.size() >= 2) {
                            string playerNick = receivedPieces[1];
                            client->setNickname(playerNick);
                            isOkay = true;

                            cout << "Connected " << playerNick << " to server!" << endl;
                        }
                    }
                    else if (playerStatus == "died") {
                        if (receivedPieces.size() >= 2) {
                            int playerScore = stoi(receivedPieces[1]);
                            client->setScore(playerScore);
                            client->setDied();
                            isOkay = true;
                        }
                    }
                }

                client->setLastQuery(isOkay);
            }
            else client->setLastQuery(!hasError);
        }
    }
    void serverSend() {
        std::vector<std::string> ids = std::vector<std::string>();
        for (auto& cl : clients) {
            ids.push_back(cl.first);
        }

        for (int keyIndex = 0; keyIndex < ids.size(); keyIndex++) {
            ServClient* client = &clients[ids[keyIndex]];
            if (!client->isSendingData() && client->hasBufferData() && client->getLastQuery()) {
                client->setSendingData(true);
                bool isSendOkay = sendToClient(*client, client->frontBuffer());
                client->popBuffer();

                if (!isSendOkay) client->setLastQuery(false);

                client->setSendingData(false);
            }
        }

        if (isStopping && isStarted()) {
            bool allDataSent = true;
            for (int keyIndex = 0; keyIndex < ids.size(); keyIndex++) {
                ServClient* client = &clients[ids[keyIndex]];
                if (client->isSendingData() || client->hasBufferData()) allDataSent = false;
            }

            if (allDataSent) stop();
        }
    }

    void process() {
        serverProcess();

        /*if (clients.size() == 0 && !isStarted()) startTicks = START_TICKS;
        else {
            bool hasNoInited = false;
            for (auto& client : clients) {
                if (!client.second.isInited()) hasNoInited = true;
            }

            startTicks = START_TICKS;
        }*/

        if (startTicks <= 0) started = true;
        else startTicks--;
    }
    bool isStarted() {
        return started;
    }
    bool isInited() {
        return inited;
    }
    string getNickname() {
        return nickname;
    }
    void setPlaying(bool val) {
        myPlaying = val;
    }
    bool isPlaying() {
        return myPlaying;
    }
    void setScore(int val) {
        myScore = val;
    }
    bool isWorking() {
        return working;
    }
    SOCKET* getSocket() {
        return &serverSocket;
    }
};

bool isPlaying = false;
PlayingModes currentMode = NONE;
PlayerObject* player;
Server* server;
Client* serverClient;
std::thread logicThread;
std::vector<PregradaObject> enemies = std::vector<PregradaObject>();
int playScore;

int main() {
    srand((unsigned)time(NULL));
    SetConsoleTitleW(L"Multiplayer Dinosaur");
    int mode = -1;

    while (mode != 0) {
        cout << "Enter playing mode (1 - single, 2 - server, 3 - client, 0 - exit): ";

        cin >> mode;
        cout << endl;

        if (mode == 0) {
            cout << "Good buy!" << endl;
        }
        else {
            if (mode == 1) currentMode = SINGLE;
            else if (mode == 2) currentMode = SERVER;
            else if (mode == 3) currentMode = CLIENT;
            else {
                cout << "Playing mode is not found" << endl;
                return -1;
            }

            bool canStart = true;
            if (currentMode == SERVER) {
                cout << "Enter your nickname > ";
                string nickname;
                cin >> nickname;

                server = new Server(nickname);
                if (!server->isInited()) canStart = false;
            }
            else if (currentMode == CLIENT) {
                cout << "Enter your nickname > ";
                string nickname;
                cin >> nickname;

                string serverAddress = "127.0.0.1";
                //cout << "Enter server IP address > ";
                //cin >> serverAddress;

                serverClient = new Client(nickname);
                serverClient->connectTo(serverAddress, 5555);
                if (!serverClient->isInited()) canStart = false;
            }

            if (canStart) {
                sizes = getConsoleSize();
                playScore = 0;
                isPlaying = true;
                player = new PlayerObject();

                std::thread connectionsThread;
                std::thread receiveThread;
                std::thread sendThread;
                if (currentMode == SERVER) {
                    server->setPlaying(isPlaying);
                    server->setScore(playScore);

                    connectionsThread = std::thread(acceptServerConnections);
                    receiveThread = std::thread(serverModeReceive);
                    sendThread = std::thread(serverModeSend);

                    std::thread waitThread = std::thread(waitForConnections);
                    waitThread.join();
                }
                else if (currentMode == CLIENT) {
                    serverClient->setInLive(true);
                    serverClient->setMyScore(playScore);

                    receiveThread = std::thread(clientModeReceive);
                    sendThread = std::thread(clientModeSend);

                    std::thread waitThread = std::thread(waitForStart);
                    waitThread.join();
                }

                logicThread = std::thread(loopLogic);
                logicThread.join();

                ClearScreen();
                if (currentMode == SINGLE) {
                    cout << "You lose with score " << to_string(playScore) << endl;
                }
                else if (currentMode == SERVER) {
                    connectionsThread.detach();
                    receiveThread.detach();
                    sendThread.detach();
                    cout << "Game closed with your`s score " << to_string(playScore) << endl;
                    server = NULL;
                }
                else if (currentMode == CLIENT) {
                    receiveThread.detach();
                    sendThread.detach();
                    cout << "Game closed with your`s score " << to_string(playScore) << endl;
                    serverClient = NULL;
                }
            }
        }
    }

    return 0;
}

pair<int, int> getConsoleSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns, rows;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return pair<int, int>(columns, rows);
}

vector<vector<char>> getGameMatrix() {
    sizes = getConsoleSize();
    vector<vector<char>> matrix = vector<vector<char>>(sizes.second - 1, vector<char>(sizes.first, ' '));

    if (isPlaying) {
        for (int floorIndex = 1; floorIndex < FLOOR_HEIGHT + 1; floorIndex++) {
            for (int widthIndex = 0; widthIndex < sizes.first; widthIndex++) {
                matrix[matrix.size() - floorIndex][widthIndex] = '#';
            }
        }

        vector<vector<char>> playerMatrix = player->getMatrix();
        for (int y = player->getPosition().Y; y < (player->getPosition().Y + playerMatrix.size()); y++) {
            int mY = y - player->getPosition().Y;
            for (int x = player->getPosition().X; x < (player->getPosition().X + playerMatrix[mY].size()); x++) {
                int mX = x - player->getPosition().X;
                matrix[y][x] = playerMatrix[mY][mX];
            }
        }

        for (auto& enemy : enemies) {
            if (enemy.isInLive()) {
                vector<vector<char>> enemyMatrix = enemy.getMatrix();

                if ((enemy.getPosition().X + enemy.getWidth()) < sizes.first) {
                    for (int y = enemy.getPosition().Y; y < (enemy.getPosition().Y + enemyMatrix.size()); y++) {
                        int mY = y - enemy.getPosition().Y;
                        for (int x = enemy.getPosition().X; x < (enemy.getPosition().X + enemyMatrix[mY].size()); x++) {
                            int mX = x - enemy.getPosition().X;
                            matrix[y][x] = enemyMatrix[mY][mX];
                        }
                    }
                }
            }
        }
    }

    vector<string> scoreStrings = vector<string>();
    if (currentMode == SINGLE) {
        string scoreString = "Score: " + std::to_string(playScore);
        scoreStrings.push_back(scoreString);
    }
    else if (currentMode == SERVER) {
        scoreStrings.push_back(server->getNickname() + (isPlaying ? "" : " (died)") + ": " + to_string(playScore));
        for (auto& sc : server->getClients()) {
            scoreStrings.push_back(sc.nickname + (sc.inLive ? "" : " (died)") + ": " + to_string(sc.score));
        }
    }
    else if (currentMode == CLIENT) {
        scoreStrings.push_back(serverClient->getNickname() + (isPlaying ? "" : " (died)") + ": " + to_string(playScore));
        for (auto& sc : serverClient->getScores()) {
            scoreStrings.push_back(sc.nickname + (sc.inLive ? "" : " (died)") + ": " + to_string(sc.score));
        }
    }

    for (int i = 0; i < scoreStrings.size(); i++) {
        int scoreX = sizes.first - scoreStrings[i].length() - 3;
        int scoreY = 5 + i;
        for (int x = scoreX; x < (scoreX + scoreStrings[i].length() + 1); x++) {
            int sX = x - scoreX;
            char ch = scoreStrings[i][sX];
            matrix[scoreY][x] = ch;
        }
    }

    return matrix;
}

void setCursorPosition(int x, int y)
{
    static const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    std::cout.flush();
    COORD coord = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(hOut, coord);
}

void ClearScreen() {
    HANDLE                     hStdOut;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD                      count;
    DWORD                      cellCount;
    COORD                      homeCoords = { 0, 0 };

    hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut == INVALID_HANDLE_VALUE) return;

    /* Get the number of cells in the current buffer */
    if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return;
    cellCount = csbi.dwSize.X * csbi.dwSize.Y;

    /* Fill the entire buffer with spaces */
    if (!FillConsoleOutputCharacter(
        hStdOut,
        (TCHAR)' ',
        cellCount,
        homeCoords,
        &count
    )) return;

    /* Fill the entire buffer with the current colors and attributes */
    if (!FillConsoleOutputAttribute(
        hStdOut,
        csbi.wAttributes,
        cellCount,
        homeCoords,
        &count
    )) return;

    /* Move the cursor home */
    SetConsoleCursorPosition(hStdOut, homeCoords);
}

bool IsPress(char symbol)
{
    return (GetKeyState(symbol) & 0x8000);
}

vector<tuple<int, int, char>> getMartrixDiff(vector<vector<char>> prev, vector<vector<char>> next) {
    vector<tuple<int, int, char>> diff = vector<tuple<int, int, char>>();
    for (int y = 0; y < next.size(); y++) {
        for (int x = 0; x < next[y].size(); x++) {
            bool forcing = false;
            char nextChar = next[y][x];
            if (y < prev.size()) {
                if (x < prev[y].size()) {
                    char prevChar = prev[y][x];
                    if (prevChar != nextChar) diff.push_back(tuple<int, int, char>(x, y, nextChar));
                }
                else forcing = true;
            }
            else forcing = true;

            if (forcing) diff.push_back(tuple<int, int, char>(x, y, nextChar));
        }
    }

    return diff;
}

int enemyGenerationTicks;
int scoreAddTicks;
vector<vector<char>> lastMatrix;
bool isLastMatrixInited;
void loopLogic() {
    enemies.clear();
    isLastMatrixInited = false;
    enemyGenerationTicks = 0;
    scoreAddTicks = TPS;

    while (isGamePlaying()) {
        if (isPlaying) {
            keyControl();
            player->processJump();

            enemyGenerationTicks--;
            if (enemyGenerationTicks <= 0) {
                int randHeight = 1 + rand() % 3;
                int randWidth = 1 + rand() % 3;
                int randNextTicks = 20 + rand() % 50;

                enemies.push_back(PregradaObject(randWidth, randHeight));
                enemyGenerationTicks = randNextTicks;
            }

            for (int enemyIndex = 0; enemyIndex < enemies.size(); enemyIndex++) {
                PregradaObject enemy = enemies[enemyIndex];
                if (enemy.isInLive()) {
                    enemy.processEnemy();

                    if (enemy.isCollisingWith(static_cast<GameObject&>(*player))) {
                        //обработка проигрыша
                        isPlaying = false;
                        player->setInLive(false);
                        /*ClearScreen();
                        cout << "You lose with score: " << playScore << endl;*/
                        break;
                    }

                    enemies[enemyIndex] = enemy;
                }

                if (!enemy.isInLive()) {
                    enemies.erase(enemies.begin() + enemyIndex);
                    enemyIndex--;
                }
            }

            if (isPlaying && player->isInLive()) {
                scoreAddTicks--;
                if (scoreAddTicks <= 0) {
                    scoreAddTicks = TPS;
                    playScore++;
                }
            }
        }

        vector<vector<char>> matrix = getGameMatrix();
        if (!isLastMatrixInited) {
            lastMatrix = matrix;
            isLastMatrixInited = true;

            ClearScreen();
            for (int i = 0; i < matrix.size(); i++) {
                vector<char> matrixLine = vector<char>(matrix[i].size());
                bool isEmptyLine = true;
                for (int j = 0; j < matrix[i].size(); j++) {
                    matrixLine[j] = matrix[i][j];
                    if (matrixLine[j] != ' ') isEmptyLine = false;
                }

                if (isEmptyLine) cout << endl;
                else cout << string(matrixLine.begin(), matrixLine.end()) << endl;
            }
        }
        else
        {
            vector<tuple<int, int, char>> diff = getMartrixDiff(lastMatrix, matrix);
            if (diff.size() > 0) {
                for (vector<tuple<int, int, char>>::const_iterator i = diff.begin(); i != diff.end(); ++i) {
                    int x = get<0>(*i);
                    int y = get<1>(*i);
                    char ch = get<2>(*i);

                    setCursorPosition(x, y);
                    cout << ch;
                }
                //setCursorPosition(0, 0);
            }

            lastMatrix = matrix;
        }

        if (currentMode == SERVER) {
            server->setPlaying(isPlaying);
            server->setScore(playScore);
            server->process();
        }
        else if (currentMode == CLIENT) {
            serverClient->setInLive(isPlaying);
            serverClient->setMyScore(playScore);
            serverClient->process();
        }

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

bool isGamePlaying() {
    bool isGamePlaying = false;
    if (currentMode == SINGLE) {
        isGamePlaying = isPlaying;
    }
    else if (currentMode == SERVER) {
        isGamePlaying = server->isWorking();
    }
    else if (currentMode == CLIENT) {
        isGamePlaying = !serverClient->isAllCompleted();
    }

    return isGamePlaying;
}

void acceptServerConnections() {
    while (server->isWorking() && !server->isStarted()) {
        SOCKADDR_IN clientAddr{};
        int clientAddrSize = sizeof(clientAddr);
        
        SOCKET client;
        if ((client = accept(*server->getSocket(), (SOCKADDR*)&clientAddr, &clientAddrSize)) != INVALID_SOCKET) {
            char value = 1;
            int modeResult = setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));

            struct sockaddr_in sin {};
            int len = sizeof(sin);

            if (getsockname(client, (struct sockaddr*)&sin, &len) != -1) {
                server->addClient(client);

                //cout << "Connected with port " << ntohs(sin.sin_port) << endl;
            }
        }

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void waitForConnections() {
    cout << "Waiting for client connections..." << endl;
    while (server->isWorking() && !server->isStarted()) {
        server->process();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void serverModeReceive() {
    while (server->isWorking()) {
        server->serverReceive();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void serverModeSend() {
    while (server->isWorking()) {
        server->serverSend();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void waitForStart() {
    cout << "Waiting for start..." << endl;
    while (!serverClient->isStarted()) {
        serverClient->process();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void clientModeReceive() {
    while (!serverClient->isAllCompleted()) {
        if (!serverClient->isInited()) continue;
        serverClient->clientReceive();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void clientModeSend() {
    while (!serverClient->isAllCompleted()) {
        if (!serverClient->isInited()) continue;
        serverClient->clientSend();

        std::chrono::milliseconds timespan((int)(1000 / TPS));
        std::this_thread::sleep_for(timespan);
    }
}

void keyControl() {
    if (IsPress(' ')) {
        //типа прыжок
        player->jump();
    }
}

vector<string> split(string str, string delimiter) {
    vector<string> pieces = vector<string>();

    auto start = 0U;
    auto end = str.find(delimiter);
    while (end != string::npos) {
        string peace = str.substr(start, end - start);
        start = end + delimiter.length();
        end = str.find(delimiter, start);
        pieces.push_back(peace);
    }
    pieces.push_back(str.substr(start, end));

    /*size_t pos = 0;
    string token;
    while ((pos = str.find(delimiter)) != string::npos) {
        token = str.substr(0, pos);
        pieces.push_back(token);
        str.erase(0, pos + delimiter.length());
    }*/

    return pieces;
}

string randomString(const int length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    string tmp_s;
    tmp_s.reserve(length);

    for (int i = 0; i < length; i++) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}