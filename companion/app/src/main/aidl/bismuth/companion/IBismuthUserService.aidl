package bismuth.companion;
interface IBismuthUserService {
    void destroy() = 16777114;
    void startBridge();
    void stopBridge();
    String getStatus();
}
