/**
 * wvw.h - Public API for the wvw library.
 * Summary: Provides a small native WebView wrapper.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#ifndef KC_WVW_H
#define KC_WVW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kc_wvw kc_wvw_t;

#define KC_WVW_OK 0
#define KC_WVW_ERROR -1

typedef void (*kc_wvw_signal_callback_t)(kc_wvw_t *ctx);

typedef struct {
    char *url;
    char *title;
    int width;
    int height;
    int fullscreen;
    int borderless;
} kc_wvw_options_t;

/**
 * Returns the build version generated at compile time.
 * @return Unix timestamp for the current build.
 */
uint64_t kc_wvw_version(void);

/**
 * Create an options struct initialized with default values.
 * @return Default-initialized options.
 */
kc_wvw_options_t kc_wvw_options_default(void);

/**
 * Load configuration from environment variables.
 * @param opts Options to update.
 * @return None.
 */
void kc_wvw_options_load_env(kc_wvw_options_t *opts);

/**
 * Free dynamically allocated resources within an options struct.
 * @param opts Options to clean up.
 * @return None.
 */
void kc_wvw_options_free(kc_wvw_options_t *opts);

/**
 * Register a handler for a library-level signal number.
 * @param ctx Window context.
 * @param sig Application-defined signal number.
 * @param cb Callback to invoke.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_on_signal(kc_wvw_t *ctx, int sig, kc_wvw_signal_callback_t cb);

/**
 * Raise a library-level signal.
 * @param ctx Window context.
 * @param sig Signal number to raise.
 * @return KC_WVW_OK if handled or KC_WVW_ERROR if no handler exists.
 */
int kc_wvw_raise_signal(kc_wvw_t *ctx, int sig);

/**
 * Store the context for later signal dispatch.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_listen_signals(kc_wvw_t *ctx);

/**
 * Connect one operating-system signal to the library dispatcher.
 * @param ctx Window context.
 * @param sig_id Operating-system signal number.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_listen_signal(kc_wvw_t *ctx, int sig_id);

/**
 * Dispatch an operating-system signal into the library signal table.
 * @param sig Operating-system signal number.
 * @return None.
 */
void kc_wvw_signal_listener(int sig);

/**
 * Create a new native WebView context.
 * @param ctx_out Destination context pointer.
 * @param opts Configuration options.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_open(kc_wvw_t **ctx_out, kc_wvw_options_t *opts);

/**
 * Release a WebView context and its resources.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_close(kc_wvw_t *ctx);

/**
 * Start the native window event loop.
 * @param ctx Window context.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_run(kc_wvw_t *ctx);

/**
 * Navigate the current WebView to a new URL.
 * @param ctx Window context.
 * @param url Destination URL.
 * @return KC_WVW_OK on success or KC_WVW_ERROR on failure.
 */
int kc_wvw_navigate(kc_wvw_t *ctx, const char *url);

#ifdef __cplusplus
}
#endif

#endif
