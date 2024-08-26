#pragma once

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define BOLDYELLOW "\033[1m\033[33m"
#define BOLDGREEN "\033[01;32m"
#define BOLDBLUE "\033[01;34m"

class FileEditor {
  public:
	FileEditor(MyFs* filesystem);
	~FileEditor();

	void run();

  private:
	void drawUI();
	void openFile(const std::string& filepath);
	void saveFile();
	void handleKeyboardShortcuts(ImGuiIO& io);

	MyFs* fs;
	GLFWwindow* window; // Declare window as a member variable
	std::string currentFilePath;
	std::string currentFileContent;
	std::string selectedFilePath;				  // For context menu actions
	std::optional<std::string> clipboardFilePath; // For copy-paste functionality
	bool showErrorPopup = false;
	std::string errorMessage;

	void showError(const std::string& errorMessage);
	void showError(const Errors& err);
};