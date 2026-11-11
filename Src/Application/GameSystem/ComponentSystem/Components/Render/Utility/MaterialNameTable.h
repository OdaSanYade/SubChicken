#pragma once

const std::vector<std::string>& MaterialNameTable();
void SetMaterialNameTable(std::vector<std::string> names);
uint32_t MaterialIdFromName(const std::string& name);
const std::string& MaterialNameFromId(uint32_t id);