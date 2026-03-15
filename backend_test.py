#!/usr/bin/env python3
"""
Backend API Testing for Protoon - Roblox Asset & Map Extraction Suite
Tests all API endpoints and functionality
"""
import requests
import sys
import time
from datetime import datetime

class ProtoonAPITester:
    def __init__(self, base_url="https://modify-6.preview.emergentagent.com"):
        self.base_url = base_url
        self.api_base = f"{base_url}/api"
        self.tests_run = 0
        self.tests_passed = 0
        self.test_results = []

    def log_test(self, name, success, details=""):
        """Log test result"""
        self.tests_run += 1
        if success:
            self.tests_passed += 1
            print(f"✅ {name}")
        else:
            print(f"❌ {name} - {details}")
        
        self.test_results.append({
            "name": name,
            "success": success,
            "details": details
        })

    def run_test(self, name, method, endpoint, expected_status=200, data=None, check_response=None):
        """Run a single API test"""
        url = f"{self.api_base}/{endpoint}" if not endpoint.startswith('http') else endpoint
        headers = {'Content-Type': 'application/json'}
        
        try:
            if method == 'GET':
                response = requests.get(url, headers=headers, timeout=10)
            elif method == 'POST':
                response = requests.post(url, json=data, headers=headers, timeout=10)
            else:
                self.log_test(name, False, f"Unsupported method: {method}")
                return False, {}

            success = response.status_code == expected_status
            
            if success and check_response:
                try:
                    response_data = response.json() if response.headers.get('content-type', '').startswith('application/json') else response.text
                    success = check_response(response_data)
                except Exception as e:
                    success = False
                    self.log_test(name, False, f"Response validation failed: {str(e)}")
                    return False, {}
            
            if success:
                self.log_test(name, True)
                try:
                    return True, response.json() if response.headers.get('content-type', '').startswith('application/json') else response.text
                except:
                    return True, response.text
            else:
                self.log_test(name, False, f"Status {response.status_code}, expected {expected_status}")
                return False, {}

        except requests.exceptions.Timeout:
            self.log_test(name, False, "Request timeout")
            return False, {}
        except requests.exceptions.ConnectionError:
            self.log_test(name, False, "Connection error")
            return False, {}
        except Exception as e:
            self.log_test(name, False, f"Error: {str(e)}")
            return False, {}

    def test_root_endpoint(self):
        """Test /api/ endpoint"""
        def check_root_response(data):
            return (
                isinstance(data, dict) and
                'message' in data and
                'version' in data and
                'tools' in data and
                isinstance(data['tools'], list)
            )
        
        return self.run_test(
            "Root API endpoint",
            "GET",
            "",
            expected_status=200,
            check_response=check_root_response
        )

    def test_tools_endpoint(self):
        """Test /api/tools endpoint"""
        def check_tools_response(data):
            if not isinstance(data, list):
                return False
            
            # Should have 3 tools: Protoon, Fleasion, USSI Script
            if len(data) != 3:
                return False
            
            tool_names = [tool.get('name') for tool in data]
            expected_tools = ['Protoon', 'Fleasion', 'USSI Script']
            
            return all(tool in tool_names for tool in expected_tools)
        
        return self.run_test(
            "Tools endpoint",
            "GET",
            "tools",
            expected_status=200,
            check_response=check_tools_response
        )

    def test_capture_status_endpoint(self):
        """Test /api/capture/status endpoint"""
        def check_status_response(data):
            required_fields = [
                'is_running', 'packets_captured', 'packets_parsed',
                'instances_created', 'properties_set', 'errors',
                'duration', 'packets_per_second'
            ]
            return all(field in data for field in required_fields)
        
        return self.run_test(
            "Capture status endpoint",
            "GET",
            "capture/status",
            expected_status=200,
            check_response=check_status_response
        )

    def test_demo_data_load(self):
        """Test /api/capture/demo endpoint"""
        def check_demo_response(data):
            return (
                isinstance(data, dict) and
                'status' in data and
                data['status'] == 'loaded' and
                'instances' in data and
                isinstance(data['instances'], int) and
                data['instances'] > 0
            )
        
        return self.run_test(
            "Load demo data",
            "POST",
            "capture/demo",
            expected_status=200,
            check_response=check_demo_response
        )

    def test_captured_instances_endpoint(self):
        """Test /api/capture/instances endpoint after demo load"""
        def check_instances_response(data):
            return (
                isinstance(data, dict) and
                'instances' in data and
                'count' in data and
                isinstance(data['instances'], list) and
                isinstance(data['count'], int)
            )
        
        return self.run_test(
            "Captured instances endpoint",
            "GET",
            "capture/instances",
            expected_status=200,
            check_response=check_instances_response
        )

    def test_ussi_download_endpoint(self):
        """Test /api/download/ussi endpoint"""
        def check_ussi_response(data):
            # USSI endpoint returns either JSON with script/download_url or lua script directly
            if isinstance(data, dict):
                return 'script' in data or 'download_url' in data
            elif isinstance(data, str):
                # Check if it's a lua script
                return data.strip().startswith('--') or 'loadstring' in data or 'synsaveinstance' in data
            return False
        
        return self.run_test(
            "USSI download endpoint",
            "GET",
            "download/ussi",
            expected_status=200,
            check_response=check_ussi_response
        )

    def test_protoon_download_endpoint(self):
        """Test /api/download/protoon endpoint"""
        def check_protoon_response(data):
            return (
                isinstance(data, dict) and
                'message' in data and
                'instructions' in data and
                isinstance(data['instructions'], list)
            )
        
        return self.run_test(
            "Protoon download endpoint",
            "GET",
            "download/protoon",
            expected_status=200,
            check_response=check_protoon_response
        )

    def test_health_check_endpoint(self):
        """Test /api/health endpoint"""
        def check_health_response(data):
            return (
                isinstance(data, dict) and
                'status' in data and
                data['status'] == 'healthy'
            )
        
        return self.run_test(
            "Health check endpoint",
            "GET",
            "health",
            expected_status=200,
            check_response=check_health_response
        )

    def run_all_tests(self):
        """Run all backend tests"""
        print("🚀 Starting Protoon Backend API Tests")
        print("=" * 50)
        
        # Test basic endpoints first
        self.test_root_endpoint()
        self.test_health_check_endpoint()
        self.test_tools_endpoint()
        
        # Test capture functionality
        self.test_capture_status_endpoint()
        
        # Test demo data loading
        demo_success, _ = self.test_demo_data_load()
        if demo_success:
            # Wait a moment for demo data to be processed
            time.sleep(0.5)
            self.test_captured_instances_endpoint()
        
        # Test download endpoints
        self.test_ussi_download_endpoint()
        self.test_protoon_download_endpoint()
        
        # Print results
        print("\n" + "=" * 50)
        print(f"📊 Test Results: {self.tests_passed}/{self.tests_run} passed")
        
        if self.tests_passed == self.tests_run:
            print("🎉 All backend tests passed!")
            return 0
        else:
            print("⚠️  Some tests failed - check details above")
            return 1

def main():
    tester = ProtoonAPITester()
    return tester.run_all_tests()

if __name__ == "__main__":
    sys.exit(main())