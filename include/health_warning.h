#pragma once
/* Publish from the Unity main thread; -1 clears immediately outside gameplay. */
void health_warning_set(int health_permille);
/* Called only with a current window EGL context, just before presentation. */
void health_warning_draw(int width,int height);
void health_warning_context_lost(void *context);
int health_warning_pending(void);
