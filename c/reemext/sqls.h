const char SQLCreateTable_Schedule[] = {
	"CREATE TABLE schedules("
	"	schid INTEGER PRIMARY KEY ASC AUTOINCREMENT,"					// �ƻ�ID
	"	create_time DATETIME,"											// ���������ƻ���ʱ��

	"	mode INTEGER,"													// ����ִ��ģʽ�����õ�ֵ������ö��ScheduleMode��
	"	flags INTEGER DEFAULT 0,"										// �����һЩ���õ���ϱ�־

	"	repeat_interval UNSIGNED INT DEFAULT 0,"						// �ظ�ִ�мƻ��ļ��ʱ�䣨��λ���룩
	"	repeat_times UNSIGNED INT DEFAULT 0,"							// �ظ�ִ�еĴ�����0��ʾ���ظ�ִ�С��ظ�ִ��ָ���Ǵﵽִ������֮��

	"	weekdays UNSIGNED INT DEFAULT 0,"								// ÿ�ܵ���Щ��ִ�У�1��ʾ��1��2��ʾ�ܶ���������7��ʹ��λ��ʶ 1 << day�����7��
	"	day_time_ranges TEXT DEFAULT ''"								// ÿ��ִ�е�ʱ�䷶Χ������һ�����飬ÿһ�б�ʾһ�죬��0������һ����6�������գ�ÿ������ж������ִ�е�ʱ��Σ�ÿ��ʱ�����,�ָ���ÿ��ʱ��ε�д����00:00-23:59
	")"
};

const char SQLCreateTable_HttpReqTasks[] = {
	"CREATE TABLE httpreq_tasks("
	"	taskid INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
	"	schid INTEGER DEFAULT 0,"
	"	url VARCHAR(280) DEFAULT '',"
	"	download_to VARCHAR(280) DEFAULT NULL,"
	"	posts TEXT DFAULT NULL,"	
	"	result_force TEXT DFAULT NULL"	
	")"
};

const char SQLCreateTable_ScriptRunTasks[] = {
	"CREATE TABLE scriptrun_tasks("
	"	taskid INTEGER PRIMARY KEY ASC AUTOINCREMENT,"
	"	schid INTEGER DEFAULT 0,"
	"	script_file VARCHAR(280) DEFAULT NULL,"
	"	script_codes TEXT DFAULT NULL"
	")"
};

