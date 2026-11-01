#include "loader_write_policy.h"

#include <string.h>

static orc_c6_loader_write_session_t session;

void orc_c6_loader_guard_arm(uint32_t offset, uint32_t size)
{
    session = (orc_c6_loader_write_session_t){
        .offset = offset, .size = size, .active = (size != 0)
    };
}

void orc_c6_loader_guard_disarm(void)
{
    memset(&session, 0, sizeof(session));
}

/* Policy helpers are exported only inside this component so the protocol
 * wrapper and focused host test exercise the same rules. */
int orc_c6_loader_allow_begin(uint32_t offset, uint32_t size,
                              uint32_t packets, uint32_t block)
{
    return session.active && !session.begun && offset == session.offset &&
        size == session.size && block == 1024 &&
        packets == ((session.size - 1u) / 1024u + 1u);
}

int orc_c6_loader_allow_data(uint32_t sequence, uint32_t size)
{
    if (!session.active || !session.begun || session.verified ||
        sequence != session.next_sequence ||
        sequence >= ((session.size - 1u) / 1024u + 1u)) return 0;
    const uint32_t remain = session.size - sequence * 1024u;
    return size == (remain > 1024u ? 1024u : remain);
}

int orc_c6_loader_allow_md5(uint32_t address, uint32_t size)
{
    return session.active && session.begun && !session.verified &&
        address == session.offset && size == session.size &&
        session.next_sequence == ((session.size - 1u) / 1024u + 1u);
}

int orc_c6_loader_allow_end(uint32_t stay)
{
    return session.active && session.verified && stay == 1;
}

void orc_c6_loader_note_success(uint8_t command)
{
    if (command == 0x02) session.begun = 1;
    else if (command == 0x03) ++session.next_sequence;
    else if (command == 0x13) session.verified = 1;
    else if (command == 0x04) orc_c6_loader_guard_disarm();
}
