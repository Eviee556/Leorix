// Visual Scripting panel: a beginner block editor drawn with ImDrawList (no node-editor library).
// The graph itself lives in tools/VisualScript.* and is evaluated without any UI.
#include "app/App.h"
#include "imgui.h"
#include <SDL3/SDL_dialog.h>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
const ImVec4 kBrassV(0.79f, 0.54f, 0.29f, 1.0f);
const ImU32 kShapeWire  = IM_COL32(242, 156, 64, 255);   // orange: shapes
const ImU32 kNumberWire = IM_COL32(90, 190, 220, 255);   // blue: numbers
const float kBlockW = 240.0f, kPad = 8.0f, kPortR = 6.0f;

ImU32 WireColor(WireKind k) { return k == WireKind::Shape ? kShapeWire : kNumberWire; }

ImU32 CategoryColor(const char* c) {
    std::string s = c;
    if (s == "Shapes")  return IM_COL32(150, 95, 45, 255);
    if (s == "Change")  return IM_COL32(70, 110, 150, 255);
    if (s == "Repeat")  return IM_COL32(110, 80, 150, 255);
    if (s == "Mix")     return IM_COL32(60, 130, 90, 255);
    if (s == "Numbers") return IM_COL32(45, 120, 140, 255);
    return IM_COL32(150, 60, 60, 255);                    // Finish
}

void Bezier(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thick = 3.0f) {
    float dx = std::max(50.0f, std::fabs(b.x - a.x) * 0.5f);
    dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, thick);
}

float DistToBezier(ImVec2 a, ImVec2 b, ImVec2 p) {
    float dx = std::max(50.0f, std::fabs(b.x - a.x) * 0.5f), best = 1e9f;
    ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
    for (int i = 0; i <= 24; ++i) {
        float t = i / 24.0f, u = 1 - t;
        float x = u*u*u*a.x + 3*u*u*t*c1.x + 3*u*t*t*c2.x + t*t*t*b.x;
        float y = u*u*u*a.y + 3*u*u*t*c1.y + 3*u*t*t*c2.y + t*t*t*b.y;
        best = std::min(best, std::hypot(p.x - x, p.y - y));
    }
    return best;
}

struct Port { int block, input; ImVec2 pos; WireKind kind; };

const SDL_DialogFileFilter kBlocksFilter[] = {{"Leorix Blocks", "leorixblocks"}};
void SDLCALL OnBlocksSave(void* app, const char* const* files, int) { if (files && files[0]) static_cast<App*>(app)->QueueBlocksPath(files[0], true); }
void SDLCALL OnBlocksOpen(void* app, const char* const* files, int) { if (files && files[0]) static_cast<App*>(app)->QueueBlocksPath(files[0], false); }
} // namespace

void App::VisualScriptingPanel() {
    ImGui::SetNextWindowSize(ImVec2(1000, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual Scripting", &showVisualScripting)) { ImGui::End(); return; }
    if (!visualStarted) { visualGraph.LoadStarter(); visualStarted = true; visualMessage = "Here's an example to start from. Press Build it! to see it in 3D."; }
    visualGraph.frame = playback.frame; visualGraph.fps = playback.fps;

    // ---- finished Save / Open dialogs (they report back on another thread)
    std::string saveTo, openFrom;
    { std::lock_guard<std::mutex> lock(importMutex); saveTo.swap(pendingBlocksSave); openFrom.swap(pendingBlocksOpen); }
    if (!saveTo.empty()) {
        if (saveTo.size() < 13 || saveTo.substr(saveTo.size() - 13) != ".leorixblocks") saveTo += ".leorixblocks";
        std::ofstream f(saveTo, std::ios::binary);
        f << SaveGraph(visualGraph);
        visualMessageOk = (bool)f;
        visualMessage = f ? "Saved your blocks." : "Couldn't save there. Try another folder.";
        if (f) blocksPath = saveTo;
    }
    if (!openFrom.empty()) {
        std::ifstream f(openFrom, std::ios::binary);
        std::stringstream text; text << f.rdbuf();
        constexpr std::streamoff kMaxFile = 16 << 20;
        if (!f || text.tellp() > kMaxFile) { visualMessage = "Couldn't open that file."; visualMessageOk = false; }
        else {
            visualMessageOk = LoadGraph(visualGraph, text.str(), visualMessage);
            if (visualMessageOk) { blocksPath = openFrom; visualPan = ImVec2(0, 0); visualNotes.clear(); visualDragFrom = -1; }
        }
    }

    // ---- top bar
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.54f, 0.37f, 0.22f, 1)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBrassV);
    bool build = ImGui::Button("Build it!", ImVec2(140, 34));
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns your blocks into a 3D model and adds it to the scene.");
    ImGui::SameLine();
    if (ImGui::Button("Start over", ImVec2(110, 34))) {
        visualGraph.LoadStarter(); visualPan = ImVec2(0, 0); visualNotes.clear();
        visualMessage = "Back to the starter example."; visualMessageOk = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Throws away your blocks and loads the starter example again.");
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(70, 34)))
        SDL_ShowSaveFileDialog(OnBlocksSave, this, window, kBlocksFilter, 1, blocksPath.empty() ? nullptr : blocksPath.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Saves your blocks to a .leorixblocks file so you can carry on later.\nSaving a .leorix project keeps them too.");
    ImGui::SameLine();
    if (ImGui::Button("Open", ImVec2(70, 34))) SDL_ShowOpenFileDialog(OnBlocksOpen, this, window, kBlocksFilter, 1, nullptr, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens blocks you saved before. Your current blocks are replaced.");
    ImGui::SameLine();
    ImGui::Checkbox("Update every frame", &visualLive);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rebuilds the model whenever the timeline moves, so Time blocks animate.\nPress Build it! once, then Play on the timeline.");
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (!visualMessage.empty())
        ImGui::TextColored(visualMessageOk ? ImVec4(0.55f, 0.85f, 0.55f, 1) : ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", visualMessage.c_str());
    for (const std::string& n : visualNotes) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), "Note: %s", n.c_str());
    ImGui::EndGroup();

    if (ImGui::CollapsingHeader("How this works")) {
        ImGui::TextWrapped(
            "Each box is a block that does one small job. Add blocks from the list on the left, then drag a wire "
            "from the dot on the right of one block to a dot on the left of another. Orange wires carry shapes, "
            "blue wires carry numbers, and they only plug into a slot of the same colour. Anything you don't plug in "
            "uses the number typed on the block. Finish by wiring your shape into Show in Scene and press Build it!. "
            "Drag a block's title bar to move it, drag empty space to look around, and right-click a block or a wire "
            "to remove it. Hover anything for help.");
    }

    // Live update: rebuild the last built model in place when the frame changes (no undo step per frame).
    if (visualLive && !build && playback.frame != visualLastFrame && LiveObjectExists(visualLiveObj)) {
        visualLastFrame = playback.frame;
        BuildResult r = EvaluateGraph(visualGraph);
        if (r.ok) {
            visualLiveObj->mesh = std::move(r.mesh);
            visualLiveObj->mesh.EnsureUVs();
            visualLiveObj->MarkTopoDirty();
            visualLiveObj->stackDirty = true;
        } else { visualMessage = r.message; visualMessageOk = false; }
    }

    // Pressing Build it! goes through the same add-object path and undo step as Add Primitive.
    if (build) {
        BuildResult r = EvaluateGraph(visualGraph);
        visualNotes = r.notes; visualMessage = r.message; visualMessageOk = r.ok;
        if (r.ok) {
            auto keep = std::make_shared<Mesh>(r.mesh);
            visualLiveObj = scene.AddMesh(std::move(r.mesh), "Visual script");
            visualLastFrame = playback.frame;
            undoStack.Push("Build Visual Script",
                [this] { if (!scene.objects.empty()) scene.Remove((int)scene.objects.size() - 1); },
                [this, keep] { scene.AddMesh(*keep, "Visual script"); });
            showViewport3D = true;
            ShowStatus("Visual script built a model");
        }
    }

    // ---- left: Add a block
    ImGui::BeginChild("##blocks", ImVec2(230, 0), ImGuiChildFlags_Borders);
    ImGui::TextColored(kBrassV, "Add a block");
    ImGui::TextDisabled("Click to add. Hover for help.");
    int catCount = 0; const char* const* cats = BlockCategories(catCount);
    for (int c = 0; c < catCount; ++c) {
        ImGui::SeparatorText(cats[c]);
        for (int t = 0; t < (int)BlockType::Count; ++t) {
            const BlockInfo& info = GetBlockInfo((BlockType)t);
            if (std::string(info.category) != cats[c]) continue;
            ImGui::PushID(t);
            if (ImGui::Button(info.name, ImVec2(-1, 0))) {
                float k = 24.0f * (float)(visualGraph.blocks.size() % 8);
                visualGraph.Add((BlockType)t, -visualPan.x + 40 + k, -visualPan.y + 40 + k);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.help);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", info.blurb);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: the canvas
    ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos(), canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    const ImVec2 origin(canvasMin.x + visualPan.x, canvasMin.y + visualPan.y);
    dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(28, 26, 24, 255));
    for (float x = std::fmod(visualPan.x, 32.0f); x < canvasSize.x; x += 32.0f) dl->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), IM_COL32(45, 42, 38, 255));
    for (float y = std::fmod(visualPan.y, 32.0f); y < canvasSize.y; y += 32.0f) dl->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), IM_COL32(45, 42, 38, 255));

    dl->ChannelsSplit(3);   // 0 wires, 1 block backgrounds, 2 block contents
    std::vector<Port> inputs;
    std::vector<std::pair<int, ImVec2>> outputs;
    int removeId = -1;
    const ImGuiIO& io = ImGui::GetIO();

    for (Block& b : visualGraph.blocks) {
        const BlockInfo& info = GetBlockInfo(b.type);
        const ImVec2 pos(origin.x + b.x, origin.y + b.y);
        ImGui::PushID(b.id);
        dl->ChannelsSetCurrent(2);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + kPad, pos.y + kPad * 0.6f));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kBlockW - 2 * kPad);
        ImGui::TextUnformatted(info.name);
        const float headerH = ImGui::GetItemRectMax().y - pos.y + kPad * 0.6f;
        ImGui::Dummy(ImVec2(0, kPad * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.72f, 0.66f, 1));
        ImGui::TextWrapped("%s", info.blurb);
        ImGui::PopStyleColor();
        ImGui::PushItemWidth(kBlockW - 2 * kPad - 100);
        for (int i = 0; i < (int)info.inputs.size(); ++i) {
            const BlockInput& in = info.inputs[i];
            const float rowY = ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() * 0.5f;
            inputs.push_back({b.id, i, ImVec2(pos.x, rowY), in.kind});
            ImGui::PushID(i);
            if (visualGraph.WireInto(b.id, i)) ImGui::Text("%s  (plugged in)", in.label);
            else if (in.kind == WireKind::Shape) ImGui::TextDisabled("%s: plug a shape in", in.label);
            else ImGui::DragFloat(in.label, &b.values[i], (in.max - in.min) / 400.0f, in.min, in.max, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", in.help);
            ImGui::PopID();
        }
        if (b.type == BlockType::Math) {
            if (ImGui::BeginCombo("A ? B", MathOpName(b.op))) {
                for (int op = 0; op < kMathOpCount; ++op) if (ImGui::Selectable(MathOpName(op), op == b.op)) b.op = op;
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick the sum: A add B, A take away B, A times B or A divide by B.");
        }
        if (b.type == BlockType::Mirror) {
            if (ImGui::BeginCombo("Flip", MirrorWayName(b.op))) {
                for (int w = 0; w < kMirrorWayCount; ++w) if (ImGui::Selectable(MirrorWayName(w), w == b.op)) b.op = w;
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which way the reflection goes: left/right, up/down or front/back.");
        }
        ImGui::PopItemWidth();
        if (!b.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", b.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const ImVec2 blockMax(pos.x + kBlockW, ImGui::GetItemRectMax().y + kPad);

        // Ports first so they win hover over the title-bar drag handle.
        if (info.hasOutput) {
            const ImVec2 out(blockMax.x, pos.y + headerH * 0.5f);
            outputs.push_back({b.id, out});
            ImGui::SetCursorScreenPos(ImVec2(out.x - 9, out.y - 9));
            ImGui::InvisibleButton("##out", ImVec2(18, 18));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && visualDragFrom < 0) visualDragFrom = b.id;
            if (ImGui::IsItemHovered() && visualDragFrom < 0)
                ImGui::SetTooltip(info.output == WireKind::Shape ? "Drag from here to plug this shape into another block." : "Drag from here to plug this number into another block.");
        }
        for (const Port& p : inputs) {
            if (p.block != b.id) continue;
            ImGui::SetCursorScreenPos(ImVec2(p.pos.x - 9, p.pos.y - 9));
            ImGui::PushID(1000 + p.input);
            ImGui::InvisibleButton("##in", ImVec2(18, 18), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (const Wire* w = visualGraph.WireInto(b.id, p.input)) {
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) visualGraph.Unplug(b.id, p.input);
                else if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && visualDragFrom < 0) {
                    visualDragFrom = w->from; visualGraph.Unplug(b.id, p.input);   // pick the wire back up
                }
                else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to move this wire, or right-click to unplug it.");
            }
            ImGui::PopID();
        }
        // Title bar (with the help tooltip), then the rest of the block body: both drag the block.
        for (int part = 0; part < 2; ++part) {
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton(part ? "##body" : "##title", ImVec2(kBlockW, part ? blockMax.y - pos.y : headerH),
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) { b.x += io.MouseDelta.x; b.y += io.MouseDelta.y; }
            if (!part && ImGui::IsItemHovered() && !ImGui::IsItemActive()) ImGui::SetTooltip("%s\n\nDrag to move. Right-click to remove.", info.help);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##blockmenu");
        }
        if (ImGui::BeginPopup("##blockmenu")) {
            if (ImGui::MenuItem("Remove this block")) removeId = b.id;
            ImGui::EndPopup();
        }

        dl->ChannelsSetCurrent(1);
        const bool bad = !b.error.empty();
        dl->AddRectFilled(pos, blockMax, IM_COL32(48, 44, 40, 245), 6.0f);
        dl->AddRectFilled(pos, ImVec2(blockMax.x, pos.y + headerH), CategoryColor(info.category), 6.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRect(pos, blockMax, bad ? IM_COL32(255, 110, 100, 255) : IM_COL32(90, 82, 72, 255), 6.0f, 0, bad ? 2.5f : 1.0f);
        if (info.hasOutput) dl->AddCircleFilled(outputs.back().second, kPortR, WireColor(info.output));
        for (const Port& p : inputs) if (p.block == b.id) {
            if (visualGraph.WireInto(b.id, p.input)) dl->AddCircleFilled(p.pos, kPortR, WireColor(p.kind));
            else dl->AddCircle(p.pos, kPortR, WireColor(p.kind), 0, 2.0f);
        }
        ImGui::PopID();
    }

    // ---- wires
    dl->ChannelsSetCurrent(0);
    auto outPos = [&](int id) { for (auto& o : outputs) if (o.first == id) return o.second; return ImVec2(0, 0); };
    auto inPos = [&](int id, int input) { for (auto& p : inputs) if (p.block == id && p.input == input) return p.pos; return ImVec2(0, 0); };
    for (const Wire& w : visualGraph.wires) {
        Block* from = visualGraph.Find(w.from);
        if (from) Bezier(dl, outPos(w.from), inPos(w.to, w.input), WireColor(GetBlockInfo(from->type).output));
    }
    if (visualDragFrom >= 0) {
        Block* from = visualGraph.Find(visualDragFrom);
        if (from) Bezier(dl, outPos(visualDragFrom), io.MousePos, WireColor(GetBlockInfo(from->type).output), 2.5f);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const Port* best = nullptr; float bestD = 14.0f;
            for (const Port& p : inputs) { float d = std::hypot(p.pos.x - io.MousePos.x, p.pos.y - io.MousePos.y); if (d < bestD) { bestD = d; best = &p; } }
            if (best) {
                std::string why;
                if (visualGraph.Connect(visualDragFrom, best->block, best->input, why)) visualMessage.clear();
                else { visualMessage = why; visualMessageOk = false; }
            }
            visualDragFrom = -1;
        }
    }
    dl->ChannelsMerge();

    // Background last, so blocks and ports get the mouse first. Drag empty space to look around.
    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::InvisibleButton("##background", canvasSize.x > 1 && canvasSize.y > 1 ? canvasSize : ImVec2(1, 1),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
        { visualPan.x += io.MouseDelta.x; visualPan.y += io.MouseDelta.y; }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        for (const Wire& w : visualGraph.wires)
            if (DistToBezier(outPos(w.from), inPos(w.to, w.input), io.MousePos) < 6.0f) { visualGraph.Unplug(w.to, w.input); break; }
    }
    if (removeId >= 0) visualGraph.Remove(removeId);
    ImGui::EndChild();
    ImGui::End();
}
