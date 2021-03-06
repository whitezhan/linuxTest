#include "Table_force_guide.h"
#include "vnFileManager.h"

namespace tbl {
int Record_force_guide::compare(u32 _GroupId,u32 _StepId) const {
	int ret;
	ret = f_GroupId.compare(_GroupId);
	if (ret) {
		return ret;
	}
	ret = f_StepId.compare(_StepId);
	if (ret) {
		return ret;
	}
	return 0;
}

void Record_force_guide::load(DataStream &stream) {
	stream >> f_GroupId;
	stream >> f_StepId;
	stream >> f_CondType;
	stream >> f_CondValue;
	stream >> f_SceneAction;
	stream >> f_SceneArea;
	stream >> f_UIAction;
	stream >> f_UIAreaName;
	stream >> f_Portrait;
	stream >> f_ShowText;
	stream >> f_AddItems;
	stream >> f_Flags;
}

Table_force_guide::Table_force_guide()
:m_version(0)
,m_records(0)
,m_size(0) {
}

Table_force_guide::~Table_force_guide() {
	fini();
}

bool Table_force_guide::init(u32 fsId, const str8& path) {
	FilePath name(fsId, path);
	if(!name.fileName.empty() && name.fileName.back() != '/') {
		name.fileName += '/';
	}
	name.fileName += "force_guide.tbl";
	FileStream* fs = FileManager::instance().open(name);
	if(!fs) {
		return false;
	}
	bool ret = false;
	try {
		ret = init(*fs);
	} catch (DataStream::Exception& ) {

	}
	fs->close();
	return ret;
}
bool Table_force_guide::init(DataStream &stream) {
	u32 head;
	stream >> head;
	if(head != kTableFileHeader) {
		return false;
	}
	RecordFormat format;
	stream >> format;
	if(format.size() != 12) {
		return false;
	}
	if(format[0].value != 131) {
		return false;
	}
	if(format[1].value != 131) {
		return false;
	}
	if(format[2].value != 3) {
		return false;
	}
	if(format[3].value != 1) {
		return false;
	}
	if(format[4].value != 3) {
		return false;
	}
	if(format[5].value != 1) {
		return false;
	}
	if(format[6].value != 3) {
		return false;
	}
	if(format[7].value != 3) {
		return false;
	}
	if(format[8].value != 1) {
		return false;
	}
	if(format[9].value != 1) {
		return false;
	}
	if(format[10].value != 1) {
		return false;
	}
	if(format[11].value != 3) {
		return false;
	}
	stream >> m_version;
#ifdef VN_2D
	DataStream::z_u32 numRecords;
	stream >> numRecords;
	m_size = numRecords.value;
#else
	_read_z_u32(stream, m_size);
#endif
	if(m_size == 0) {
		return true;
	}
	m_records = vnnew Record_force_guide[m_size];
	for(size_t i = 0; i < m_size; ++i) {
		m_records[i].load(stream);
	}
	return true;
}

void Table_force_guide::fini() {
	VN_SAFE_DELETE_ARRAY(m_records);
	m_size = 0;
}

u32 Table_force_guide::version() const {
	return m_version;
}

size_t Table_force_guide::size() const {
	return m_size;
}

const Record_force_guide * Table_force_guide::at(size_t index) const{
	if(index < m_size) {
		return m_records + index;
	}
	return 0;
}

size_t Table_force_guide::find(u32 _GroupId,u32 _StepId) {
	ssize_t low = 0, high = (ssize_t)m_size - 1, mid;
	while(low <= high) {
		mid = (high + low)/2;
		int ret = m_records[mid].compare(_GroupId,_StepId);
		if(ret > 0) {
			high = mid - 1;
			continue;
		}
		if (ret < 0) {
			low = mid + 1;
			continue;
		}
		return mid;
	}
	return -1;
}
}
