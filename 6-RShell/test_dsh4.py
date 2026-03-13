#!/usr/bin/env python3
"""
Test suite for dsh (Drexel Shell) Part 4 - Remote Shell
Tests client-server communication, network protocol, and remote command execution
"""

import subprocess
import pytest
import time
import socket
import os
import signal
import sys

# Server configuration
TEST_PORT = 12345  # Use different port to avoid conflicts
TEST_HOST = "127.0.0.1"


class ServerProcess:
    """Helper class to manage server process"""
    
    def __init__(self, port=TEST_PORT):
        self.port = port
        self.process = None
    
    def start(self):
        """Start server in background"""
        # Start server with specific port
        self.process = subprocess.Popen(
            ['./dsh', '-s', '-p', str(self.port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # Wait for server to be ready
        time.sleep(0.5)
        
        # Check if server started successfully
        if self.process.poll() is not None:
            stdout, stderr = self.process.communicate()
            raise Exception(f"Server failed to start:\nSTDOUT: {stdout}\nSTDERR: {stderr}")
        
        # Verify server is listening
        if not self.wait_for_server():
            self.stop()
            raise Exception("Server not listening on port")
        
        return True
    
    def stop(self):
        """Stop server gracefully or forcefully"""
        if self.process:
            try:
                # Try graceful shutdown first
                self.process.terminate()
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                # Force kill if needed
                self.process.kill()
                self.process.wait()
    
    def wait_for_server(self, timeout=5):
        """Wait for server to start listening"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                sock.connect((TEST_HOST, self.port))
                sock.close()
                return True
            except (ConnectionRefusedError, socket.timeout, OSError):
                time.sleep(0.1)
        return False


def run_client_commands(commands, port=TEST_PORT, timeout=10):
    """
    Run client with list of commands
    
    Args:
        commands: List of command strings to send
        port: Port to connect to
        timeout: Timeout in seconds
    
    Returns:
        (returncode, stdout, stderr)
    """
    # Join commands with newlines
    input_text = '\n'.join(commands) + '\n'
    
    try:
        result = subprocess.run(
            ['./dsh', '-c', '-p', str(port)],
            input=input_text,
            capture_output=True,
            text=True,
            timeout=timeout
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "TIMEOUT"


@pytest.fixture(scope="function")
def server():
    """Pytest fixture to start/stop server for each test"""
    srv = ServerProcess(port=TEST_PORT)
    srv.start()
    yield srv
    srv.stop()


class TestServerStartup:
    """Test server can start and stop"""
    
    def test_server_starts(self, server):
        """Test: Server starts successfully"""
        assert server.process is not None
        assert server.process.poll() is None  # Still running
    
    def test_server_listens_on_port(self, server):
        """Test: Server listens on configured port"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.connect((TEST_HOST, TEST_PORT))
            sock.close()
        except ConnectionRefusedError:
            pytest.fail("Server not listening on port")


class TestClientConnection:
    """Test client can connect to server"""
    
    def test_client_connects(self, server):
        """Test: Client connects to server"""
        returncode, stdout, stderr = run_client_commands(['exit'])
        
        assert returncode == 0
        assert 'socket client mode' in stdout
    
    def test_client_connection_refused_no_server(self):
        """Test: Client handles connection refused gracefully"""
        # No server running
        returncode, stdout, stderr = run_client_commands(['exit'], port=54321, timeout=3)
        
        # Should fail to connect - check for any connection error
        assert (returncode != 0 or 
                len(stderr) > 0 or 
                'error' in stderr.lower() or 
                'refused' in stderr.lower() or
                'invalid' in stderr.lower())


class TestSimpleCommands:
    """Test simple command execution"""
    
    def test_echo_command(self, server):
        """Test: echo hello"""
        returncode, stdout, stderr = run_client_commands([
            'echo hello',
            'exit'
        ])
        
        assert returncode == 0
        assert 'hello' in stdout
    
    def test_pwd_command(self, server):
        """Test: pwd shows current directory"""
        returncode, stdout, stderr = run_client_commands([
            'pwd',
            'exit'
        ])
        
        assert returncode == 0
        assert '/' in stdout  # Path separator
    
    def test_ls_command(self, server):
        """Test: ls lists files"""
        returncode, stdout, stderr = run_client_commands([
            'ls',
            'exit'
        ])
        
        assert returncode == 0
        assert len(stdout) > 0  # Some output


class TestMultipleCommands:
    """Test multiple commands in sequence"""
    
    def test_multiple_echo_commands(self, server):
        """Test: Multiple echo commands"""
        returncode, stdout, stderr = run_client_commands([
            'echo first',
            'echo second',
            'echo third',
            'exit'
        ])
        
        assert returncode == 0
        assert 'first' in stdout
        assert 'second' in stdout
        assert 'third' in stdout
    
    def test_mixed_commands(self, server):
        """Test: Mix of different commands"""
        returncode, stdout, stderr = run_client_commands([
            'echo test',
            'pwd',
            'ls',
            'exit'
        ])
        
        assert returncode == 0
        assert 'test' in stdout


class TestPipelines:
    """Test piped commands work remotely"""
    
    def test_simple_pipe(self, server):
        """Test: echo hello | cat"""
        returncode, stdout, stderr = run_client_commands([
            'echo hello | cat',
            'exit'
        ])
        
        assert returncode == 0
        assert 'hello' in stdout
    
    def test_ls_pipe_grep(self, server):
        """Test: ls | grep makefile"""
        returncode, stdout, stderr = run_client_commands([
            'ls | grep makefile',
            'exit'
        ])
        
        assert returncode == 0
        # Should see makefile if it exists
    
    def test_three_command_pipe(self, server):
        """Test: echo test | cat | cat"""
        returncode, stdout, stderr = run_client_commands([
            'echo test | cat | cat',
            'exit'
        ])
        
        assert returncode == 0
        assert 'test' in stdout


class TestBuiltinCommands:
    """Test built-in commands work remotely"""
    
    def test_cd_command(self, server):
        """Test: cd /tmp, then pwd"""
        returncode, stdout, stderr = run_client_commands([
            'cd /tmp',
            'pwd',
            'exit'
        ])
        
        assert returncode == 0
        assert '/tmp' in stdout
    
    def test_exit_command(self, server):
        """Test: exit command closes client"""
        returncode, stdout, stderr = run_client_commands([
            'echo before exit',
            'exit',
            'echo after exit'  # Should not execute
        ])
        
        assert returncode == 0
        assert 'before exit' in stdout
        assert 'after exit' not in stdout


class TestStopServer:
    """Test stop-server command"""
    
    def test_stop_server_command(self, server):
        """Test: stop-server stops the server"""
        returncode, stdout, stderr = run_client_commands([
            'echo before stop',
            'stop-server'
        ])
        
        # Client should exit
        assert returncode == 0
        
        # Wait a moment for server to stop
        time.sleep(0.5)
        
        # Server should have stopped
        assert server.process.poll() is not None


class TestMultipleClients:
    """Test server handles multiple clients sequentially"""
    
    def test_two_clients_sequential(self, server):
        """Test: Two clients connect in sequence"""
        # First client
        returncode1, stdout1, stderr1 = run_client_commands([
            'echo client1',
            'exit'
        ])
        
        assert returncode1 == 0
        assert 'client1' in stdout1
        
        # Second client
        returncode2, stdout2, stderr2 = run_client_commands([
            'echo client2',
            'exit'
        ])
        
        assert returncode2 == 0
        assert 'client2' in stdout2
    
    def test_multiple_clients_sequential(self, server):
        """Test: Multiple clients in sequence"""
        for i in range(3):
            returncode, stdout, stderr = run_client_commands([
                f'echo client{i}',
                'exit'
            ])
            assert returncode == 0
            assert f'client{i}' in stdout


class TestEOFProtocol:
    """Test EOF marker protocol"""
    
    def test_commands_complete(self, server):
        """Test: Commands complete (EOF marker works)"""
        returncode, stdout, stderr = run_client_commands([
            'echo hello',
            'echo world',
            'exit'
        ], timeout=5)
        
        # Should not timeout (EOF marker works)
        assert returncode == 0
        assert 'hello' in stdout
        assert 'world' in stdout


class TestLongOutput:
    """Test handling of commands with lots of output"""
    
    def test_ls_long(self, server):
        """Test: ls with detailed output"""
        returncode, stdout, stderr = run_client_commands([
            'ls -la',
            'exit'
        ])
        
        assert returncode == 0
        assert len(stdout) > 0
    
    def test_multiple_files(self, server):
        """Test: Command that produces many lines"""
        returncode, stdout, stderr = run_client_commands([
            'ls',
            'exit'
        ])
        
        assert returncode == 0


class TestErrorHandling:
    """Test error handling"""
    
    def test_command_not_found(self, server):
        """Test: Non-existent command"""
        returncode, stdout, stderr = run_client_commands([
            'notacommand',
            'exit'
        ])
        
        # Should handle error gracefully
        assert returncode == 0  # Client exits normally
    
    def test_empty_command(self, server):
        """Test: Empty command line"""
        returncode, stdout, stderr = run_client_commands([
            '',
            'echo hello',
            'exit'
        ])
        
        assert returncode == 0
        assert 'hello' in stdout


class TestComplexScenarios:
    """Test complex real-world scenarios"""
    
    def test_complex_pipeline(self, server):
        """Test: Complex pipeline"""
        returncode, stdout, stderr = run_client_commands([
            'ls | sort',
            'exit'
        ])
        
        assert returncode == 0
    
    def test_cd_and_execute(self, server):
        """Test: cd then execute commands"""
        returncode, stdout, stderr = run_client_commands([
            'cd /tmp',
            'pwd',
            'ls',
            'exit'
        ])
        
        assert returncode == 0
        assert '/tmp' in stdout


class TestStress:
    """Stress tests"""
    
    def test_many_commands(self, server):
        """Test: Many commands in sequence"""
        commands = [f'echo command{i}' for i in range(10)]
        commands.append('exit')
        
        returncode, stdout, stderr = run_client_commands(commands, timeout=15)
        
        assert returncode == 0
        for i in range(10):
            assert f'command{i}' in stdout
    
    def test_long_command_line(self, server):
        """Test: Long command with many arguments"""
        returncode, stdout, stderr = run_client_commands([
            'echo ' + ' '.join([f'arg{i}' for i in range(50)]),
            'exit'
        ])
        
        assert returncode == 0


# Run tests with: pytest test_dsh4.py -v
if __name__ == "__main__":
    pytest.main([__file__, "-v", "--tb=short"])