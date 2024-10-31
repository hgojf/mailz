#ifndef MAILZ_CONTENT_H
#define MAILZ_CONTENT_H
enum {
	IMSG_CNT_IGNORE,
	IMSG_CNT_RETAIN,
	IMSG_CNT_LETTER,
	IMSG_CNT_LETTERPIPE,
	IMSG_CNT_REFERENCE,
	IMSG_CNT_REFERENCEOVER,
	IMSG_CNT_REPLY,
	IMSG_CNT_SUMMARY
};

#define CNT_MSGID_LEN 300
#define CNT_PFD 3

#define CNT_LR_NOHDR 0x1

struct content_header {
	char name[996];
};

struct content_reference {
	char id[CNT_MSGID_LEN];
};

struct content_reply_summary {
	char name[65];
	struct {
		char addr[255];
		char name[65];
	} reply_to;
	char message_id[CNT_MSGID_LEN];
	char in_reply_to[CNT_MSGID_LEN];
};

struct content_summary {
	time_t date;
	char from[255];
	char subject[245];
	int have_subject;
};
#endif /* MAILZ_CONTENT_H */
