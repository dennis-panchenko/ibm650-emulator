/* Punched cards, decks and the 533 Card Read Punch on synchronizer 1.
 *
 * A card is 80 columns, each holding one Hollerith character as it would
 * be printed along the top of the card: digits, letters for a digit with
 * a zone punch, ' ' for a blank column. Signs are punched over the units
 * column of a word (OperMan p.17, 46):
 *
 *     plus  (12 over 0-9):  {  A  B  C  D  E  F  G  H  I
 *     minus (11 over 0-9):  }  J  K  L  M  N  O  P  Q  R
 *
 * A plain digit in the units column reads as plus. A deck file is plain
 * text, one card per line; a short line has blank columns at the end.
 *
 * The control panel is a fixed board rather than a plugboard: every card
 * reads columns 1-80 into words 1-8 of the read buffer (columns 1-10 =
 * word 1, high-order digit first), words 9-10 are +0, and punching is the
 * mirror image. A load card is one with a 12 punch in column 1 (OperMan
 * p.46: "a 12 punch in any specific column", chosen by panel wiring).
 *
 * The read feed (OperMan pp.20-25): hopper -> first read -> second read
 * -> stacker. Read Start runs three cards in, the first one's data
 * landing in the read buffer; each RD copies the buffer to the drum and
 * feeds one more card. When the hopper runs dry three unprocessed cards
 * are left in the feed, and the reader waits until more cards arrive or
 * the operator presses End of File. */
#ifndef I650_CARDS_H
#define I650_CARDS_H

#include "types.h"

/* The card, deck and 533 types and constants live in types.h. */

/* --- Cards and decks --- */

/* Appends a copy of card; false if out of memory. */
bool i650_deck_add(struct i650_deck *d, const struct i650_card *card);
void i650_deck_clear(struct i650_deck *d);

/* Parses a deck file (NUL-terminated text). Lines longer than 80
 * characters, or with characters outside printable ASCII, are errors:
 * *bad_line gets the 1-based line number. A final line without a
 * newline is still a card; an empty line is a blank card. */
enum i650_deck_parse { I650_DECK_OK, I650_DECK_TOO_LONG, I650_DECK_BAD_CHAR,
                       I650_DECK_NO_MEMORY };
enum i650_deck_parse i650_deck_parse(const char *text, struct i650_deck *out,
                                     unsigned *bad_line);

/* One card as a deck-file line, trailing blanks dropped. */
void i650_card_text(const struct i650_card *card, char out[I650_CARD_COLUMNS + 1]);

/* A card from text (shorter text = blank columns at the end). */
void i650_card_from_text(const char *text, struct i650_card *out);

/* The 10 read-buffer words a card produces on the fixed board. Returns
 * false if any of words 1-8 isn't all digits (a blank or non-numeric
 * column -- a read validity error); bad then holds the first bad column
 * (1-based). Zone punches outside the units column are ignored. */
bool i650_card_to_words(const struct i650_card *card, struct i650_word out[I650_SYNC_WORDS],
                        unsigned *bad);

/* As read on a load card: same layout, but no validity check -- an
 * unreadable column reads as 0. */
void i650_card_to_words_load(const struct i650_card *card,
                             struct i650_word out[I650_SYNC_WORDS]);

bool i650_card_is_load(const struct i650_card *card);

/* Adds the load (12) punch to column 1, over whatever digit is there. */
void i650_card_mark_load(struct i650_card *card);

/* Words 1-8 punched into a card, sign over each units column (12 for
 * plus, 11 for minus). Words 9-10 aren't punched. */
void i650_card_from_words(const struct i650_word words[I650_SYNC_WORDS],
                          struct i650_card *out);

/* --- The 533 (operator side; the program side is RD/WR in cpu.c) --- */

/* Puts a deck in the hopper behind any cards already there. */
bool i650_533_hopper_add(struct i650_533 *u, const struct i650_deck *deck);

/* Read Start: runs cards in from the hopper until the feed holds three
 * (or the hopper is empty). Returns the number of cards fed. */
unsigned i650_533_read_start(struct i650_533 *u);

/* End of File: lets the program read the cards left in the feed with the
 * hopper empty. Also runs in whatever is left in the hopper (the manual's
 * Start-then-EOF for decks under four cards). False (no effect) unless
 * the hopper is empty and there are cards to process. */
bool i650_533_end_of_file(struct i650_533 *u);

/* Clears the hopper and the read feed into the read stacker (a non-
 * process runout) and drops End of File. */
void i650_533_runout(struct i650_533 *u);

void i650_533_free(struct i650_533 *u);

/* Cards left in the hopper. */
size_t i650_533_hopper_count(const struct i650_533 *u);

#endif
