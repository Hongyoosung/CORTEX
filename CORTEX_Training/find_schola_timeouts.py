"""
Find all timeout configurations in Schola package.
"""
import os
import re
import schola

schola_path = os.path.dirname(schola.__file__)

print("=" * 80)
print("Searching for timeout configurations in Schola")
print("=" * 80)
print(f"Schola path: {schola_path}\n")

timeout_findings = []

# Search all Python files
for root, dirs, files in os.walk(schola_path):
    for file in files:
        if file.endswith('.py'):
            filepath = os.path.join(root, file)
            relpath = os.path.relpath(filepath, schola_path)

            try:
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()

                # Find timeout= assignments
                timeout_matches = re.findall(r'timeout\s*=\s*([0-9.]+)', content, re.IGNORECASE)
                if timeout_matches:
                    timeout_findings.append({
                        'file': relpath,
                        'type': 'assignment',
                        'values': timeout_matches
                    })

                # Find .result(timeout=X)
                result_timeout_matches = re.findall(r'\.result\(\s*timeout\s*=\s*([0-9.]+)\)', content)
                if result_timeout_matches:
                    timeout_findings.append({
                        'file': relpath,
                        'type': 'result_timeout',
                        'values': result_timeout_matches
                    })

                # Find grpc call timeouts
                grpc_timeout_matches = re.findall(r'(stub\.\w+\([^)]*timeout\s*=\s*([0-9.]+)[^)]*\))', content)
                if grpc_timeout_matches:
                    timeout_findings.append({
                        'file': relpath,
                        'type': 'grpc_stub_call',
                        'calls': grpc_timeout_matches
                    })

            except Exception as e:
                pass

# Print findings
if timeout_findings:
    print("FOUND TIMEOUT CONFIGURATIONS:\n")
    for finding in timeout_findings:
        print(f"File: {finding['file']}")
        print(f"Type: {finding['type']}")
        if 'values' in finding:
            print(f"Values: {', '.join(finding['values'])}")
        elif 'calls' in finding:
            for call, timeout_val in finding['calls']:
                print(f"  {call[:80]}... (timeout={timeout_val})")
        print()
else:
    print("✓ No explicit timeout configurations found")
    print("  (This is good - means interceptor timeout should work)")

print("=" * 80)
