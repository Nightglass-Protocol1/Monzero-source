package org.monzero.miner;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.text.method.LinkMovementMethod;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.text.HtmlCompat;

import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity implements MinerService.Listener {
    private EditText address, pool, worker;
    private SeekBar threads;
    private TextView threadsLabel, hashrate, shares, state, logView;
    private CheckBox charging, light;
    private Button toggle;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        address = findViewById(R.id.address);
        pool = findViewById(R.id.pool);
        worker = findViewById(R.id.worker);
        threads = findViewById(R.id.threads);
        threadsLabel = findViewById(R.id.threads_label);
        charging = findViewById(R.id.only_charging);
        light = findViewById(R.id.light_mode);
        hashrate = findViewById(R.id.hashrate);
        shares = findViewById(R.id.shares);
        state = findViewById(R.id.state);
        logView = findViewById(R.id.log);
        toggle = findViewById(R.id.toggle);

        TextView about = findViewById(R.id.about);
        about.setText(HtmlCompat.fromHtml(getString(R.string.about), HtmlCompat.FROM_HTML_MODE_LEGACY));
        about.setMovementMethod(LinkMovementMethod.getInstance());

        MinerSettings s = MinerSettings.load(this);
        address.setText(s.address);
        pool.setText(s.pool);
        worker.setText(s.worker);
        threads.setMax(MinerSettings.cores() - 1);
        threads.setProgress(Math.min(s.threads, MinerSettings.cores()) - 1);
        charging.setChecked(s.onlyWhileCharging);
        light.setChecked(s.lightMode);
        showThreads();
        threads.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                showThreads();
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
            }
        });
        toggle.setOnClickListener(v -> onToggle());

        if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(this,
                Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        MinerService.setListener(this);
    }

    @Override
    protected void onPause() {
        MinerService.setListener(null);
        super.onPause();
    }

    private void showThreads() {
        threadsLabel.setText(getString(R.string.threads_label, threads.getProgress() + 1, MinerSettings.cores()));
    }

    private MinerSettings readForm() {
        MinerSettings s = MinerSettings.load(this);
        s.address = address.getText().toString().trim();
        s.pool = pool.getText().toString().trim();
        s.worker = worker.getText().toString().trim();
        s.threads = threads.getProgress() + 1;
        s.onlyWhileCharging = charging.isChecked();
        s.lightMode = light.isChecked();
        return s;
    }

    private void onToggle() {
        MinerService.State current = MinerService.state();
        if (current != MinerService.State.STOPPED && current != MinerService.State.FAILED) {
            startService(new Intent(this, MinerService.class).setAction(MinerService.ACTION_STOP));
            return;
        }
        MinerSettings s = readForm();
        int problem = s.problem();
        if (problem != 0) {
            Toast.makeText(this, problem, Toast.LENGTH_LONG).show();
            return;
        }
        s.save(this);
        if (!s.onlyWhileCharging) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.battery_warning_title)
                    .setMessage(R.string.battery_warning)
                    .setPositiveButton(R.string.action_start, (d, w) -> start())
                    .setNegativeButton(android.R.string.cancel, null)
                    .show();
        } else {
            start();
        }
    }

    private void start() {
        ContextCompat.startForegroundService(this,
                new Intent(this, MinerService.class).setAction(MinerService.ACTION_START));
    }

    @Override
    public void onStatus(MinerService.Status status, List<String> log) {
        boolean running = status.state != MinerService.State.STOPPED && status.state != MinerService.State.FAILED;
        toggle.setText(running ? R.string.action_stop : R.string.action_start);
        for (View v : new View[]{address, pool, worker, threads, charging, light}) v.setEnabled(!running);
        hashrate.setText(status.hashrate10s < 0 ? "—"
                : String.format(Locale.getDefault(), "%.1f H/s", status.hashrate10s));
        shares.setText(getString(R.string.shares, status.accepted, status.rejected));
        String label;
        switch (status.state) {
            case MINING:
                label = getString(R.string.state_mining);
                break;
            case PAUSED_UNPLUGGED:
            case PAUSED_HOT:
                label = getString(R.string.state_paused);
                break;
            case FAILED:
                label = getString(R.string.state_failed);
                break;
            default:
                label = getString(R.string.state_stopped);
        }
        if (!status.message.isEmpty()) label += " · " + status.message;
        else if (status.poolHeight > 0) label += " · " + getString(R.string.height, status.poolHeight);
        state.setText(label);
        StringBuilder text = new StringBuilder();
        int from = Math.max(0, log.size() - 30);
        for (int i = from; i < log.size(); i++) text.append(log.get(i)).append('\n');
        logView.setText(text);
    }
}
