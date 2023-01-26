// F4SE
#include "f4se/PluginAPI.h"
#include "f4se/PapyrusNativeFunctions.h"
#include "f4se/GameCustomization.h"
#include "f4se/PapyrusStruct.h"
#include "f4se/GameReferences.h"
#include "f4se/PapyrusVM.h"
#include "f4se/PapyrusUtilities.h"
#include "f4se/GameThreads.h"

#include "f4se/GameRTTI.h"
#include "f4se/GameObjects.h"

// Common
#include "f4se_common/f4se_version.h"
#include <set>
#include <shlobj.h>

static F4SEPapyrusInterface* g_papyrus = NULL;
F4SETaskInterface* g_task = nullptr;

class F4EESkinUpdate : public ITaskDelegate
{
public:
	F4EESkinUpdate(TESForm* form);
	virtual ~F4EESkinUpdate() { };
	virtual void Run() override;

protected:
	UInt32					m_formId;
};

F4EESkinUpdate::F4EESkinUpdate(TESForm* form)
{
	m_formId = form->formID;
}

void F4EESkinUpdate::Run()
{
	TESForm* form = LookupFormByID(m_formId);
	if (!form)
		return;

	Actor* actor = DYNAMIC_CAST(form, TESForm, Actor);
	if (!actor)
		return;

	TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
	if (!npc)
		return;

	CALL_MEMBER_FN(actor, QueueUpdate)(false, 0, true, 0);
	_MESSAGE("Refreshed head");
}


namespace LMSE {
	DECLARE_STRUCT(TintMask, "TestClass");

	TESRace* GetActorRace(Actor* actor)
	{
		TESRace* race = actor->race;
		if (!race) {
			TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
			if (npc)
				race = npc->race.race;
		}


		return race;
	}

	void Test(StaticFunctionTag *base) {
		_MESSAGE("Hello World");
	}

	tArray<BGSCharacterTint::Entry*>* GetAll(Actor* actor) {
		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);

		tArray<BGSCharacterTint::Entry*>* tints = npc->tints;

		PlayerCharacter* pPC = DYNAMIC_CAST(actor, Actor, PlayerCharacter);

		if (pPC && pPC->tints)
			tints = pPC->tints;

		return tints;
	}

	UInt32 GetIndexById(tArray<BGSCharacterTint::Entry*>* tints, UInt32 id) {
		BGSCharacterTint::Entry* tint;
		for (int i = 0; i < tints->count; i++) {
			if (tints->GetNthItem(i, tint) && tint->tintIndex == id) {
				return i;
			}
		}
		return -1;
	}

	BGSCharacterTint::Entry* GetById(Actor* actor, UInt32 id) {
		tArray<BGSCharacterTint::Entry*>* tints = GetAll(actor);
		BGSCharacterTint::Entry* tint;
		tints->GetNthItem(GetIndexById(tints, id), tint);
		return nullptr;
	}

	TintMask ConvertEntry(BGSCharacterTint::Entry* tint) {
		BGSCharacterTint::PaletteEntry* palette = static_cast<BGSCharacterTint::PaletteEntry*>(tint);

		TintMask tintMask;

		UInt32 id = tint->tintIndex;
		SInt32 colorID = palette->colorID;
		UInt32 percent = tint->percent;

		tintMask.Set("type", tint->GetType());
		tintMask.Set("color", palette->color.bgra);
		tintMask.Set("colorID", colorID);
		tintMask.Set("percent", percent);

		return tintMask;
	}

	TintMask Get(StaticFunctionTag*, Actor* actor, UInt32 id) {
		BGSCharacterTint::Entry* entry = GetById(actor, id);
		return ConvertEntry(entry);
	}

	VMArray<TintMask> GetAll(StaticFunctionTag*, Actor* actor) {
		VMArray<TintMask> tints;
		tArray<BGSCharacterTint::Entry*>* entries = GetAll(actor);
		BGSCharacterTint::Entry* entry;

		for (int i = 0; i < entries->count; i++) {
			if (entries->GetNthItem(i, entry)) {
				TintMask tint = ConvertEntry(entry);
				tints.Push(&tint);
			}
		}

		return tints;
	}

	void Update(StaticFunctionTag*, Actor* actor) {
		_MESSAGE("Updating the actor");

		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		if (actor == (*g_player)) {
			CopyCharacterTints((*g_player)->tints, npc->tints);
		}

		_MESSAGE("Finished updating the actor");
	}

	void Add(StaticFunctionTag*, Actor* actor, TintMask mask)
	{
		_MESSAGE("Adding new overlay");
		UInt32 id;
		UInt32 type;
		UInt32 color;
		SInt32 colorID;
		UInt32 percent;

		mask.Get("id", &id);
		mask.Get("type", &type);
		mask.Get("color", &color);
		mask.Get("colorid", &colorID);
		mask.Get("percent", &percent);

		
		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		UInt8 gender = CALL_MEMBER_FN(npc, GetSex)();
		TESRace* race = GetActorRace(actor);
		auto chargenData = race->chargenData[gender];

		BGSCharacterTint::Template::Entry* templateEntry = chargenData->GetTemplateByIndex(id);

		if (templateEntry) {
			_MESSAGE("Found template entry");
			BGSCharacterTint::Entry* newEntry = CreateCharacterTintEntry((id << 16) | type);
			
			if (newEntry) {
				_MESSAGE("Created new entry");
				if (newEntry->GetType() == BGSCharacterTint::Entry::kTypePalette) {
					BGSCharacterTint::PaletteEntry* palette = static_cast<BGSCharacterTint::PaletteEntry*>(newEntry);
					BGSCharacterTint::Template::Palette* paletteTemplate = static_cast<BGSCharacterTint::Template::Palette*>(templateEntry);

					palette->color.bgra = color;
					auto colorData = paletteTemplate->GetColorDataByID(colorID);
					if (colorData)
						palette->colorID = colorID;
					else if (paletteTemplate->colors.count)
						palette->colorID = paletteTemplate->colors[0].colorID;
					else
						palette->colorID = 0;
				}
				newEntry->percent = percent;
				npc->tints->Push(newEntry);

				
				if (actor == (*g_player)) {
					_MESSAGE("Copying character tints");
					CopyCharacterTints((*g_player)->tints, npc->tints);
				}

				npc->MarkChanged(0x800); // Save FaceData
				npc->MarkChanged(0x4000); // Save weights

				g_task->AddTask(new F4EESkinUpdate(actor));
				_MESSAGE("Finished adding overlay with id");
			}
			else {
				_MESSAGE("Entry couldn't be created");
			}
		}
		else {
			_MESSAGE("No overlay template found");
		}

		
	}

	void Set(StaticFunctionTag*, Actor* actor, TintMask mask) {
		UInt32 id;
		SInt32 type;
		UInt32 color;
		UInt32 colorID;
		UInt32 percent;

		mask.Get("id", &id);
		mask.Get("color", &color);
		mask.Get("colorid", &colorID);
		mask.Get("percent", &percent);

		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		UInt8 gender = CALL_MEMBER_FN(npc, GetSex)();
		TESRace* race = GetActorRace(actor);
		auto chargenData = race->chargenData[gender];

		BGSCharacterTint::Template::Entry* templateEntry = chargenData->GetTemplateByIndex(id);

		BGSCharacterTint::Entry* entry = GetById(actor, id);
		BGSCharacterTint::PaletteEntry* palette = static_cast<BGSCharacterTint::PaletteEntry*>(entry);
		BGSCharacterTint::Template::Palette* paletteTemplate = static_cast<BGSCharacterTint::Template::Palette*>(templateEntry);

		entry->percent = percent;
		palette->color.bgra = color;
		auto colorData = paletteTemplate->GetColorDataByID(colorID);
		if (colorData)
			palette->colorID = colorID;
		else if (paletteTemplate->colors.count)
			palette->colorID = paletteTemplate->colors[0].colorID;
		else
			palette->colorID = 0;
	}

	void Remove(StaticFunctionTag*, Actor* actor, UInt32 id) {
		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		npc->tints->Remove(GetIndexById(npc->tints, id));
	}

	void RemoveAll(StaticFunctionTag*, Actor* actor) {
		TESNPC* npc = DYNAMIC_CAST(actor->baseForm, TESForm, TESNPC);
		npc->tints->Clear();
	}
}

bool RegisterFuncs(VirtualMachine* vm) {

	vm->RegisterFunction(
		new NativeFunction0 <StaticFunctionTag, void>("Test", "TestClass", LMSE::Test, vm));
	
	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, void, Actor*, LMSE::TintMask>("Add", "TestClass", LMSE::Add, vm));

	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, void, Actor*, UInt32>("Remove", "TestClass", LMSE::Remove, vm));

	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, LMSE::TintMask, Actor*, UInt32>("Get", "TestClass", LMSE::Get, vm));

	vm->RegisterFunction(
		new NativeFunction2<StaticFunctionTag, void, Actor*, LMSE::TintMask>("Set", "TestClass", LMSE::Set, vm));

	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, Actor*>("RemoveAll", "TestClass", LMSE::RemoveAll, vm));

	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, VMArray<LMSE::TintMask>, Actor*>("GetAll", "TestClass", LMSE::GetAll, vm));

	vm->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, Actor*>("Update", "TestClass", LMSE::Update, vm));
	
	return true;
}

/* Plugin Query */

extern "C" {
	bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info) {
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Fallout4\\F4SE\\f4se_example_plugin.log");
		gLog.SetPrintLevel(IDebugLog::kLevel_Error);
		gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);

		// populate info structure
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "f4se_example_plugin";
		info->version = 1;

		if (f4se->isEditor) {
			_MESSAGE("loaded in editor, marking as incompatible");

			return false;
		}
		else if (f4se->runtimeVersion != RUNTIME_VERSION_1_10_163) {
			_MESSAGE("unsupported runtime version %d", f4se->runtimeVersion);

			return false;
		}
		// ### do not do anything else in this callback
		// ### only fill out PluginInfo and return true/false

		_MESSAGE("F4SEPlugin_Query Loaded");

		// supported runtime version
		return true;
	}

	bool F4SEPlugin_Load(const F4SEInterface * f4se) {
		g_papyrus = (F4SEPapyrusInterface *)f4se->QueryInterface(kInterface_Papyrus);
		g_task = (F4SETaskInterface*)f4se->QueryInterface(kInterface_Task);

		if (g_papyrus->Register(RegisterFuncs))
			_MESSAGE("F4SEPlugin_Load Funcs Registered");

		_MESSAGE("F4SEPlugin_Load Loaded");

		return true;
	}

};
