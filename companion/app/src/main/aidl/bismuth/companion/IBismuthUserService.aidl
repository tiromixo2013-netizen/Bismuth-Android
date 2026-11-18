package bismuth.companion;
interface IBismuthUserService {
    void destroy() = 16777114;
    void startBridge() = 1;
    void stopBridge() = 2;
    String getStatus() = 3;
}
