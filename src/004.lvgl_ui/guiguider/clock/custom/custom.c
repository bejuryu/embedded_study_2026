/*
 * Copyright 2024 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */

/*********************
 *      INCLUDES
 *********************/
#include "custom.h"

#include <stdio.h>
#include <time.h>

#include "lvgl.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void timer_clock_callback(lv_timer_t* timer) {
  lv_ui* ui = (lv_ui*)lv_timer_get_user_data(timer);

  time_t rawtime;
  struct tm* timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);

  char buf[16];
  strftime(buf, sizeof(buf), "%Y.%m.%d", timeinfo);
  lv_label_set_text(ui->screen_label_date, buf);
  strftime(buf, sizeof(buf), "%H", timeinfo);
  lv_label_set_text(ui->screen_label_hour, buf);
  strftime(buf, sizeof(buf), "%M", timeinfo);
  lv_label_set_text(ui->screen_label_min, buf);
  strftime(buf, sizeof(buf), "%S", timeinfo);
  lv_label_set_text(ui->screen_label_sec, buf);
  lv_obj_set_style_text_opa(ui->screen_label_clock_colon, LV_OPA_100 * (timeinfo->tm_sec % 2 ? 1 : 0.6), 0);
  static const char* day_kr[] = {" 일요일", " 월요일", " 화요일", " 수요일", " 목요일", " 금요일", " 토요일"};
  lv_label_set_text(ui->screen_label_week, day_kr[timeinfo->tm_wday]);
}

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_timer_t* timer_clock = NULL;

/**
 * Create a demo application
 */

void custom_init(lv_ui* ui) {
  /* Add your codes here */
  timer_clock = lv_timer_create(timer_clock_callback, 1000, ui);
}
