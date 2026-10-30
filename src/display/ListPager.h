/**
 * @file ListPager.h
 * @brief The page arithmetic of the list screens, which has no hardware in it: how many pages a list makes, where a page index is
 *        held when the list shrank under it, and one step of a pager arrow. No Arduino: the host tests run them
 *        (test/test_validators) and the Ângulo's glue calls them (DisplayManager_UiA_Menus.cpp: the frame and the touch of the
 *        settings list, the alarms list and the status pages), so what the tests pin is the code the panel runs.
 */
#pragma once

/* How many pages `n` rows make at `perPage` rows to a page: rounded up, so a last page that is not full still counts, and no rows
 * make no page. n >= 0 and perPage >= 1. */
inline int listPages( int n, int perPage ) {
	return ( n + perPage - 1 ) / perPage;
}

/* A page index held to the list that exists, when the list shrank under it: below the first page it is the first, and past the last
 * it is the last (the first, when there are no pages at all). The clamp below comes first on purpose. In the order the glue first
 * spelled the two lines (past the last, then below the first) the second line caught the -1 that the `pages > 0` test is there to keep
 * out, so a header that dropped the test gave the same value for every input and no case could fail; in this order both lines carry
 * weight, and the value is the glue's for every input the host model covers. */
inline int listClampPage( int page, int pages ) {
	if ( page < 0 ) page = 0;
	if ( page >= pages ) page = pages > 0 ? pages - 1 : 0;
	return page;
}

/* One step of a pager arrow, `dir` < 0 back and otherwise forward, held at the ends: the original's arrows on the settings list, the
 * alarms list and the status pages wrap around, and the Ângulo's pager stops (the licence's arrows stop in both drivers). A step does
 * not pull in a page that is past the end, when the list shrank under it: listClampPage( ) does that. */
inline int listPageStep( int page, int pages, int dir ) {
	if ( dir < 0 ) return page > 0 ? page - 1 : page;
	return page < pages - 1 ? page + 1 : page;
}
