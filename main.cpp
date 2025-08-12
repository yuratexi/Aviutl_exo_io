#include "pch.h"


#define EXO_INPUT (WM_USER + 0x1)
#define EXO_OUTPUT (WM_USER + 0x2)
static const char exo_input[] = ("exoファイルを一気にインポート");
static const char exo_output[] = ("exoファイルを一気にエクスポート");


//設定画面のチェックボックスの設定
constexpr TCHAR* checkbox_name[] = { "objがないシーンをエクスポートしない" };
constexpr int checkbox_default[] = { 1 };


FILTER* exeditfp;

EXFUNC exfunc;

ExEdit::ExoHeader exoheader;

int* ObjectAlloc_ptr; // 0x1e0fa0
ExEdit::Object** ObjectArray_ptr; // 0x1e0fa4
int* SelectingObjectNum_ptr; // 0x167d88
int* SelectingObjectIndex; // 0x179230
int* split_mode; // 0x1538b0

static inline void(__cdecl* drawtimeline)(); // 39230

AviUtlInternal g_auin;
HINSTANCE g_instance = 0;


FILTER* get_exeditfp(AviUtl::FilterPlugin* fp) {
	AviUtl::SysInfo si;
	fp->exfunc->get_sys_info(NULL, &si);

	for (int i = 0; i < si.filter_n; i++) {
		FILTER* tfp = (FILTER*)fp->exfunc->get_filterp(i);
		if (tfp->information != NULL) {
			if (!strcmp(tfp->information, "拡張編集(exedit) version 0.92 by ＫＥＮくん")) return tfp;
		}
	}
	return NULL;
}


BOOL func_init(AviUtl::FilterPlugin* fp)
{


	// 拡張編集関連のアドレスを取得する。
	if (!g_auin.initExEditAddress())
		return FALSE;

	return TRUE;
}

BOOL func_exit(AviUtl::FilterPlugin* fp)
{


	return TRUE;
}

void ReplaceAll(std::string& stringreplace, const std::string& origin, const std::string& dest)
{
    size_t pos = 0;
    size_t offset = 0;
    size_t len = origin.length();
    // 指定文字列が見つからなくなるまでループする
    while ((pos = stringreplace.find(origin, offset)) != std::string::npos) {
        stringreplace.replace(pos, len, dest);
        offset = pos + dest.length();
    }
}


std::string wstringToString(const std::wstring& wstr)
{
    int size = WideCharToMultiByte(CP_ACP, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_ACP, 0, &wstr[0], (int)wstr.size(), &result[0], size, NULL, NULL);
    return result;
}


/// <summary>
/// エクスプローラーでフォルダを選択するダイアログを出現させるinputbox
/// </summary>
/// <param name="args">inputboxに表示する文字列</param>
/// <returns>選択されたフォルダパス(std::string)</returns>
std::string input_explorer_box(std::string title) {
    std::string output;

    // COM を初期化
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        return output;
    }

    // IFileDialog を作成
    IFileDialog* pFileDialog = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileDialog));
    if (FAILED(hr)) {
        CoUninitialize();
        return output;
    }

    // IFileDialog をフォルダー選択モードに設定
    DWORD dwOptions;
    pFileDialog->GetOptions(&dwOptions);
    pFileDialog->SetOptions(dwOptions | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS);


    // タイトルを設定
    pFileDialog->SetTitle(std::wstring(title.begin(), title.end()).c_str());

    // ダイアログを表示
    hr = pFileDialog->Show(NULL);
    if (SUCCEEDED(hr)) {
        // 選択されたフォルダーのパスを取得
        IShellItem* pItem;
        hr = pFileDialog->GetResult(&pItem);
        if (SUCCEEDED(hr)) {
            PWSTR pszFilePath;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
            if (SUCCEEDED(hr)) {
                output = wstringToString(pszFilePath);
                CoTaskMemFree(pszFilePath);
            }
            pItem->Release();
        }
    }

    // IFileDialog を解放
    pFileDialog->Release();

    // COM を終了
    CoUninitialize();

    return output;
}

void f_exo_input(AviUtl::EditHandle* editp, AviUtl::FilterPlugin* fp) {


    int scene_qty = 49; //使えるシーンの数が増えることがあればこの値を変更すればよい

    std::string scene_path;
    while (true) {
        scene_path = input_explorer_box("Select folder to import");
        if (PathFileExistsA(scene_path.c_str())) break; //フォルダが存在したらループを抜ける
        if (scene_path.find("\\") == std::string::npos) return;  //キャンセルされたら処理を終了(\が含まれていなかったらパスが入力されていないのでキャンセルとみなす)
        if (scene_path == (std::string)"キャンセルされました") return;  //キャンセルされたら処理を終了
        MessageBoxA(fp->hwnd, "フォルダが存在しません。存在するフォルダを選択してください", fp->name, MB_OK);
    }

    scene_path += '\\';

    std::string scene_path_work;

    scene_path_work = scene_path;

    g_auin.SetScene(0, g_auin.GetFilter(fp, "拡張編集"), editp);  //シーン番号を0(root)に設定
    scene_path = scene_path + "scene_root.exo";

    g_auin.LoadExo(scene_path.c_str(), 0, 0, fp, editp); //exoファイル読み込み


    for (int i = 1; i <= scene_qty; ++i) {
        g_auin.SetScene(i, g_auin.GetFilter(fp, "拡張編集"), editp);

        scene_path = scene_path_work + "scene_" + std::to_string(i) + ".exo";

        if (!PathFileExistsA(scene_path.c_str())) continue; //フォルダが存在しなかったら処理をスキップ

        g_auin.LoadExo(scene_path.c_str(), 0, 0, fp, editp); //exoファイル読み込み
    }
    g_auin.SetScene(0, g_auin.GetFilter(fp, "拡張編集"), editp);  //シーン番号を0(root)に設定
}


void f_exo_output(AviUtl::EditHandle* editp, AviUtl::FilterPlugin* fp) {

    int scene_qty = 49; //使えるシーンの数が増えることがあればこの値を変更すればよい

    std::string scene_path;
    while (true) {
        scene_path = input_explorer_box("Select folder to save");

        if (PathFileExistsA(scene_path.c_str())) break; //フォルダが存在したらループを抜ける
        if (scene_path.find("\\") == std::string::npos) return;  //キャンセルされたら処理を終了(\が含まれていなかったらパスが入力されていないのでキャンセルとみなす)
        MessageBoxA(fp->hwnd, "フォルダが存在しません。存在するフォルダを選択してください", fp->name, MB_OK);
    }
    scene_path += '\\';

    std::string scene_path_work;

    scene_path_work = scene_path;

    g_auin.SetScene(0, g_auin.GetFilter(fp, "拡張編集"), editp);  //シーン番号を0(root)に設定
    scene_path = scene_path + "scene_root.exo";


    g_auin.SaveExo(scene_path.c_str()); //exoファイル作成


    for (int i = 1; i <= scene_qty; ++i) {
        g_auin.SetScene(i, g_auin.GetFilter(fp, "拡張編集"), editp);

        if (g_auin.GetCurrentSceneObjectCount() <= 0 && checkbox_default[0] == 1) continue; //シーンのオブジェクト数が0かつエクスポートしないにチェックが入っていたら処理をしない
        scene_path = scene_path_work +"scene_" + std::to_string(i) + ".exo";

        g_auin.SaveExo(scene_path.c_str()); //exoファイル作成
    }
    g_auin.SetScene(0, g_auin.GetFilter(fp, "拡張編集"), editp);  //シーン番号を0(root)に設定
}

BOOL func_WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, AviUtl::EditHandle* editp, AviUtl::FilterPlugin* fp)
{


	switch (message)
	{
	case WM_FILTER_INIT:
		//拡張編集0.92があるかの判別
		exeditfp = get_exeditfp(fp);
		if (exeditfp == NULL) {
			MessageBoxA(fp->hwnd, "拡張編集0.92が見つかりませんでした", fp->name, MB_OK);
			break;
		}

		//ショートカットキーの追加
		fp->exfunc->add_menu_item(fp, (LPSTR)exo_input, fp->hwnd, EXO_INPUT, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);
		fp->exfunc->add_menu_item(fp, (LPSTR)exo_output, fp->hwnd, EXO_OUTPUT, NULL, (AviUtl::ExFunc::AddMenuItemFlag)NULL);


		ObjectAlloc_ptr = (int*)((int)exeditfp->dll_hinst + 0x1e0fa0);
		ObjectArray_ptr = (ExEdit::Object**)((int)exeditfp->dll_hinst + 0x1e0fa4);
		SelectingObjectNum_ptr = (int*)((int)exeditfp->dll_hinst + 0x167d88);
		SelectingObjectIndex = (int*)((int)exeditfp->dll_hinst + 0x179230);
		split_mode = (int*)((int)exeditfp->dll_hinst + 0x1538b0);
		drawtimeline = reinterpret_cast<decltype(drawtimeline)>((int)exeditfp->dll_hinst + 0x39230);

		break;


	case WM_FILTER_COMMAND:
		switch (wParam) {
        case EXO_INPUT:
            f_exo_input(editp, fp);
            break;
        case EXO_OUTPUT:
            f_exo_output(editp, fp);
            break;
		}
		break;
	}

	return FALSE;
}

EXTERN_C BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		{
			// ロケールを設定する。
			// これをやらないと日本語テキストが文字化けするので最初に実行する。
			_tsetlocale(LC_CTYPE, _T(""));



			// この DLL のハンドルをグローバル変数に保存しておく。
			g_instance = instance;


			break;
		}
	case DLL_PROCESS_DETACH:
		{


			break;
		}
	}

	return TRUE;
}



EXTERN_C AviUtl::FilterPluginDLL* WINAPI GetFilterTable()
{


	// 設定を読み込む。
	//loadConfig();

	LPCSTR name = "exo_IO";
	LPCSTR information = "exoファイルのインポート・エクスポート";

    static AviUtl::FilterPluginDLL filter =
    {
        .flag =
            AviUtl::FilterPluginDLL::Flag::AlwaysActive | AviUtl::FilterPluginDLL::Flag::WindowThickFrame,
        .name = name,
        .check_n = 1,
        .check_name = const_cast<const char **>(checkbox_name),
        .check_default = const_cast<int*>(checkbox_default),
		.func_init = func_init,
		.func_exit = func_exit,
		.func_WndProc = func_WndProc,
		.information = information,
	};


	return &filter;
}

//--------------------------------------------------------------------
