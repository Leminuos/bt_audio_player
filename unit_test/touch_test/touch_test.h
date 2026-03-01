#ifndef _TOUCH_TEST_H_
#define _TOUCH_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test touch calibration.
 * Reads raw touch coordinates and prints them to help with calibration.
 * Also shows a visual indicator on the LVGL screen.
 */
void test_touch_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* _TOUCH_TEST_H_ */
