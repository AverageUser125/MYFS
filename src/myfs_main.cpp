#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <iostream>
#include <optional>
#include "myfs.hpp" // Include your filesystem class
#include "EntryInfo.hpp"
#include "myfs_main.hpp"
#include "errorReporting.hpp"
#include <misc/cpp/imgui_stdlib.h>

FileEditor::FileEditor(MyFs* filesystem) : fs(filesystem), window(nullptr) {
}

FileEditor::~FileEditor() {
	// The OS will do this automaticlly

	//if (window) {
	//	ImGui_ImplOpenGL3_Shutdown();
	//	ImGui_ImplGlfw_Shutdown();
	//	ImGui::DestroyContext();
	//	glfwDestroyWindow(window);
	//	glfwTerminate();
	//}
}


void FileEditor::run() {
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return;
	}

	window = glfwCreateWindow(1280, 720, "File Editor", NULL, NULL);
	if (!window) {
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cerr << "Failed to initialize GLAD" << std::endl;
		return;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;	  // IF using Docking Branch
	(void)io;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	enableReportGlErrors();

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		drawUI();
		handleKeyboardShortcuts(io);

		if (showErrorPopup) {
			ImGui::OpenPopup("Error");
		}

		if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("%s", errorMessage.c_str());
			if (ImGui::Button("OK")) {
				ImGui::CloseCurrentPopup();
				showErrorPopup = false;
			}
			ImGui::EndPopup();
		}

		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}
}

// Callback function for handling input text resizing
int InputTextCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		// Handle resizing
		auto* buffer = static_cast<std::vector<char>*>(data->UserData);
		buffer->resize(data->BufSize);
		data->Buf = buffer->data();
	}
	return 0;
}

void FileEditor::drawUI() {
	static std::string newFileName = "newFile.txt";
	static std::string newDirName = "newDirectory";
	static std::string renameBuffer;
	static bool isRenaming = false;
	static bool isCreatingFile = false;
	static bool isCreatingDir = false;
	static bool isHoverFile = false;

	ImGui::Begin("File Explorer & Editor", nullptr,
					ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowPos(ImVec2(0, 0));
	ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);

	ImGui::Columns(2);

	ImGui::BeginChild("File Explorer", ImVec2(0, 0), true);
	auto entries = fs->listTree();
	isHoverFile = false;
	for (const auto& entry : entries) {
		// Set color for directories and files
		if (entry.type == DIRECTORY_TYPE) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 1.0f, 1.0f)); // Light Blue color
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Default color
		}

		if (ImGui::Selectable(entry.path.c_str(), entry.path == selectedFilePath)) {
			selectedFilePath = entry.path;
		}

		// Check for double-click after handling selection
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
			openFile(entry.path);
		}

		// Pop the color style
		ImGui::PopStyleColor();

		// Context menu for each item
		if (ImGui::BeginPopupContextItem()) {
			isHoverFile = true;
			if (ImGui::MenuItem("Open")) {
				openFile(entry.path);
			}
			if (ImGui::MenuItem("Delete")) {
				Errors error = fs->remove(selectedFilePath);
				if (error != OK) {
					showError(error);
				}
			}
			if (ImGui::MenuItem("Rename")) {
				renameBuffer = entry.path; // Pre-fill with the current name
				isRenaming = true;
			}
			
			ImGui::EndPopup();
		}
	}

	if (isCreatingFile) {
		ImGui::InputText("New File Name", &newFileName);
		if (ImGui::Button("Create")) {
			std::string newFilePath = MyFs::addCurrentDir(newFileName, selectedFilePath);
			Errors error = fs->createFile(newFilePath);
			if (error != OK) {
				showError(error);
			} else {
				isCreatingFile = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			isCreatingFile = false;
		}
	}

	if (isCreatingDir) {
		ImGui::InputText("New Directory Name", &newDirName);
		if (ImGui::Button("Create")) {
			std::string newDirPath = MyFs::addCurrentDir(newDirName, selectedFilePath);
			Errors error = fs->createDirectory(newDirPath);
			if (error != OK) {
				showError(error);
			}
			isCreatingDir = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			isCreatingDir = false;
		}
	}
	if (isRenaming) {
		ImGui::InputText("Rename To", &renameBuffer);
		if (ImGui::Button("Rename")) {
			std::string newFilePath = MyFs::addCurrentDir(renameBuffer, MyFs::splitPath(selectedFilePath).first);
			if (newFilePath == selectedFilePath) {

				isRenaming = false;
				renameBuffer.clear(); // Clear input if the name didn't change
			} else {

				Errors err = fs->move(selectedFilePath, newFilePath);
				if (err != OK) {
					showError(err);
				} else {
					// update the editor to the new file name
					if (selectedFilePath == currentFilePath) {
						currentFilePath = newFilePath;
					}
					selectedFilePath = newFilePath;
					isRenaming = false;
					renameBuffer.clear(); // Clear input after renaming
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			isRenaming = false;
			renameBuffer.clear(); // Clear input if cancelled
		}
	}

	if (!isHoverFile) {

		// Context menu for creating files or directories
		if (ImGui::BeginPopupContextWindow()) {
			if (ImGui::MenuItem("Create File")) {
				newFileName = "newFile.txt";
				isCreatingFile = true;
			}
			if (ImGui::MenuItem("Create Directory")) {
				newDirName = "newDirectory";
				isCreatingDir = true;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	ImGui::NextColumn();

	ImGui::BeginChild("Editor", ImVec2(0, 0), true);

	if (!currentFilePath.empty()) {
		ImGui::Text("Editing: %s", currentFilePath.c_str());

		// Define size for the input text area
		ImVec2 size = ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 16);

		// Use InputTextMultiline with std::string
		if (ImGui::InputTextMultiline("##source", &currentFileContent, size, ImGuiInputTextFlags_None)) {
			// Content is automatically updated in currentFileContent
		}
		if (ImGui::Button("Save")) {
			saveFile();
		}
	} else {
		ImGui::Text("No file selected.");
	}

	ImGui::EndChild();

	ImGui::End();
}


void FileEditor::openFile(const std::string& filepath) {
	std::string content;
	Errors error = fs->getContent(filepath, content);
	if (error == OK) {
		currentFilePath = filepath;
		currentFileContent = content;
	} else {
		showError(error);
	}
}

void FileEditor::saveFile() {
	Errors error = fs->setContent(currentFilePath, currentFileContent);
	if (error != OK) {
		showError(error);
	}
}

void FileEditor::handleKeyboardShortcuts(ImGuiIO& io) {
	if (ImGui::IsKeyPressed(ImGuiKey_C)) {
		if (!selectedFilePath.empty()) {
			clipboardFilePath = selectedFilePath;
		}
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_V)) {
		if (clipboardFilePath && !selectedFilePath.empty()) {
			Errors error = fs->copy(*clipboardFilePath,
									MyFs::addCurrentDir(fs->splitPath(*clipboardFilePath).second, selectedFilePath));
			if (error != OK) {
				showError(error);
			}
		}
	} else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
		Errors error = fs->remove(selectedFilePath);
		if (error != OK) {
			showError(error);
		}
	}

}

void FileEditor::showError(const Errors& err) {
	this->errorMessage = MyFs::errToString(err);
	showErrorPopup = true;
}

void FileEditor::showError(const std::string& errorMessage) {
	this->errorMessage = errorMessage;
	showErrorPopup = true;
}

int main(int argc, char** argv) {
	BlockDeviceSimulator blkdevsim(RESOURCES_PATH "test.bin"); // Assuming this is your block device simulator
	MyFs filesystem(&blkdevsim);

	FileEditor editor(&filesystem);
	editor.run();

	return 0;
}
