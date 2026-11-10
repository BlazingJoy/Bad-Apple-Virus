#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

using Frame = vector<vector<int>>;// ID, x, y, w, h, visibility
vector<Frame> Video;
int boxnum = 0;

static bool GetRes(const wchar_t* name, const char*& data, DWORD& size) {
    HRSRC h = FindResourceW(NULL, name, MAKEINTRESOURCEW(10));// 10 means RT_RCDATA
    if (!h) return false;
    HGLOBAL g = LoadResource(NULL, h);
    if (!g) return false;
    size = SizeofResource(NULL, h);
    data = (const char*)LockResource(g);
    return data != nullptr;
}

static bool LoadVideo() {
    const char* data = nullptr;
    DWORD size = 0;
    if (!GetRes(L"BADAPPLE_JSON", data, size)) {// This must match RC file
        MessageBoxA(NULL, "JSON resource missing", "Error", MB_OK);
        return false;
    }
    try {
        json root = json::parse(data, data + size);
        Video = root.get<vector<Frame>>();
    } catch (const exception& e) {
        MessageBoxA(NULL, e.what(), "JSON error", MB_OK);
        return false;
    }
    for (auto& fr : Video) boxnum = max(boxnum, (int)fr.size());
    return !Video.empty();
}

static std::wstring g_wavPath;
static double g_fps = 30.0;
static bool StartAudio() {
    const char* data = nullptr;
    DWORD size = 0;
    if (!GetRes(L"BADAPPLE_WAV", data, size)) {//bad_apple.wav, This must match rc file
        MessageBoxW(NULL, L"WAV resource missing", L"Audio error", MB_OK);
        return false;
    }

    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) {
        MessageBoxW(NULL, L"Couldn't find temp folder", L"Audio error", MB_OK);
        return false;
    }
    g_wavPath = std::wstring(tmp) + L"badapple_tmp.wav";

    {
        std::filesystem::path p(g_wavPath);
        std::ofstream out(p, std::ios::binary);
        if (!out) {
            MessageBoxW(NULL, (L"Couldn't write temp file:\n" + g_wavPath).c_str(), L"Audio error", MB_OK);
            return false;
        }
        out.write(data, size);
    }

    std::wstring cmd = L"open \"" + g_wavPath + L"\" type waveaudio alias bgm";
    MCIERROR err = mciSendStringW(cmd.c_str(), NULL, 0, NULL);
    if (err) {
        wchar_t msg[256];
        mciGetErrorStringW(err, msg, 256);
        MessageBoxW(NULL, msg, L"MCI error", MB_OK);
        return false;
    }
    mciSendStringW(L"set bgm time format milliseconds", NULL, 0, NULL);
    wchar_t lenBuf[64] = {};
    if (mciSendStringW(L"status bgm length", lenBuf, 64, NULL) == 0) {
        double len = _wtoi(lenBuf) / 1000.0;
        if (len > 0 && !Video.empty()) g_fps = Video.size() / len;
    }
    return mciSendStringW(L"play bgm", NULL, 0, NULL) == 0;
}

static double AudioSeconds() {
    wchar_t buf[64] = {};
    if (mciSendStringW(L"status bgm position", buf, 64, NULL) != 0) return -1.0;
    return _wtoi(buf) / 1000.0;
}

static void StopAudio() {
    mciSendStringW(L"close bgm", NULL, 0, NULL);
    if (!g_wavPath.empty()) DeleteFileW(g_wavPath.c_str());
}

DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX; //This gets the squarish shape of the windows

static const double AUDIO_OFFSET = 0.0;
static size_t g_lastFrame = (size_t)-1;

template <class DERIVED_TYPE>
class BaseWindow {
public:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        DERIVED_TYPE* pThis = NULL;
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCTW* pCreate = (CREATESTRUCTW*)lParam;
            pThis = (DERIVED_TYPE*)pCreate->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hwnd = hwnd;
        }
        else
            pThis = (DERIVED_TYPE*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (pThis)
            return pThis->HandleMessage(uMsg, wParam, lParam);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    BaseWindow() : m_hwnd(NULL) {}

    BOOL Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle = 0,
                int x = CW_USEDEFAULT, int y = CW_USEDEFAULT,
                int nWidth = CW_USEDEFAULT, int nHeight = CW_USEDEFAULT,
                HWND hWndParent = 0, HMENU hMenu = 0) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DERIVED_TYPE::WindowProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = ClassName();
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        m_hwnd = CreateWindowExW(dwExStyle, ClassName(), lpWindowName, dwStyle,x, y, nWidth, nHeight, hWndParent, hMenu,GetModuleHandleW(NULL), this);
        if (m_hwnd) {
            int cornerPreference = 1;// 1 means square corners
            DwmSetWindowAttribute(m_hwnd, 33 ,&cornerPreference, sizeof(cornerPreference));
        }
        return m_hwnd ? TRUE : FALSE;
    }
    HWND Window() const { return m_hwnd; }

protected:
    virtual PCWSTR ClassName() const = 0;
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
    HWND m_hwnd;
};

class MainWindow : public BaseWindow<MainWindow> {
public:
    MainWindow(int x = 0, int y = 0, int width = 100, int height = 100) {
        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
        vis = 0; // 0 is hidden, 1 is shown
    }

    PCWSTR ClassName() const { return L"Window"; }

    static void SetWindows(MainWindow* list, int count) {
        allWindows = list;
        windowCount = count;
    }

    UINT hideshow() {
        UINT flags = 0;
        if (vis == 1) flags = SWP_SHOWWINDOW;
        if (vis == 0) flags = SWP_HIDEWINDOW;
        return flags;
    }

    static void UpdateAll(const vector<vector<int>>& frame) {
        for (const vector<int>& box : frame) {
            allWindows[box[0]].move(box[1], box[2]);
            allWindows[box[0]].resize(box[3], box[4]);
            allWindows[box[0]].vis = box[5];
        }

        HDWP hDwp = BeginDeferWindowPos(windowCount);

        if (hDwp) {
            for (const vector<int>& box : frame) {
                hDwp = DeferWindowPos(
                    hDwp,
                    allWindows[box[0]].Window(),
                    NULL,
                    allWindows[box[0]].x * allWindows[box[0]].Wunit - 3,//Windows uses an invisible edge, that gives more space for users to resize the wondow
                    allWindows[box[0]].y * allWindows[box[0]].Hunit - 3,//The 3,6 is to hard code the adjustment
                    allWindows[box[0]].width * allWindows[box[0]].Wunit + 6,//I could not remove the invisible edge
                    allWindows[box[0]].height * allWindows[box[0]].Hunit + 6,
                    SWP_NOZORDER | SWP_NOACTIVATE | allWindows[box[0]].hideshow()
                );

                if (!hDwp) break;
            }

            if (hDwp) EndDeferWindowPos(hDwp);
        }
    }

    int count = 0;
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_GETMINMAXINFO: {
                MINMAXINFO* m = (MINMAXINFO*)lParam;
                m->ptMinTrackSize.x = 0;
                m->ptMinTrackSize.y = 0;
                return 0;
            }
            case WM_TIMER:
                if (m_hwnd == allWindows[0].Window()) {
                    double secs = AudioSeconds();
                    if (secs < 0) return 0;

                    secs += AUDIO_OFFSET;
                    size_t frame = (size_t)(secs * g_fps);

                    if (frame >= Video.size()) { PostQuitMessage(0); return 0; }
                    if (frame != g_lastFrame) {
                        UpdateAll(Video[frame]);
                        g_lastFrame = frame;
                    }
                }
                return 0;

            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    PostQuitMessage(0);
                    return 0;
                }
                break;

            case WM_LBUTTONDOWN:
                PostQuitMessage(0);
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(m_hwnd, &ps);
                FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(WHITE_BRUSH));
                EndPaint(m_hwnd, &ps);
                return 0;
            }

            case WM_DESTROY:
                return 0;
        }
        return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
    }

    void move(int newX, int newY) {
        x = newX;
        y = newY;
    }
    void resize(int newW, int newH) {
        width = newW;
        height = newH;
    }

    int X() const { return x; }
    int Y() const { return y; }
    int Width() const { return width; }
    int Height() const { return height; }

private:
    int x, y;
    int width, height;
    int vis;
    static MainWindow* allWindows;
    static int windowCount;
    double screenWidth = GetSystemMetrics(SM_CXSCREEN);
    double screenHeight = GetSystemMetrics(SM_CYSCREEN);
    double refactor = 8.0;//This represents how much you compress the original video
    double Wunit = screenWidth / (480 / refactor);//480 is the width of the original video
    double Hunit = screenHeight / (360 / refactor);//360 is the height of the original video
};

MainWindow* MainWindow::allWindows = nullptr;
int MainWindow::windowCount = 0;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    if (!LoadVideo()) return 0;

    MainWindow* windows = new MainWindow[boxnum];
    MainWindow::SetWindows(windows, boxnum);

    for (int i = 0; i < boxnum; i++) {
        std::wstring name = L"Bad Apple";
        if (!windows[i].Create(name.c_str(), style, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 0, 0, 100, 100)) return 0;
    }
    MainWindow::UpdateAll(Video[0]);

    if (!StartAudio()) {
        delete[] windows;
        return 0;
    }

    SetTimer(windows[0].Window(), 1, 16, NULL);
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    StopAudio();
    delete[] windows;
    return 0;
}
