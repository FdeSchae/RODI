from setuptools import setup, find_packages

setup(
    name='benthic_models',
    version='0.0.1',
    packages=find_packages(include=['benthic_models']),
    package_dir={'':'src'}
)