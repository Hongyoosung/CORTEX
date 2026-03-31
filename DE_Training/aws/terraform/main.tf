###############################################################################
# DE v10.2 — AWS Infrastructure
# Terraform >= 1.5  |  Provider: hashicorp/aws ~> 5.0
###############################################################################

terraform {
  required_version = ">= 1.5"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }

  # Optional remote state — uncomment and fill in your bucket/key
  # backend "s3" {
  #   bucket = "your-tfstate-bucket"
  #   key    = "de/de-v10-2/terraform.tfstate"
  #   region = "us-east-1"
  # }
}

provider "aws" {
  region = var.aws_region
}

###############################################################################
# Variables
###############################################################################

variable "aws_region" {
  description = "AWS region to deploy into"
  type        = string
  default     = "ap-northeast-2"
}

variable "project" {
  description = "Project tag applied to all resources"
  type        = string
  default     = "de-v10-2"
}

variable "environment" {
  description = "Deployment environment (dev / staging / prod)"
  type        = string
  default     = "dev"
}

variable "ssh_public_key_path" {
  description = "Path to the SSH public key used for EC2 access"
  type        = string
  default     = "~/.ssh/id_rsa.pub"
}

variable "allowed_ssh_cidr" {
  description = "CIDR block allowed to SSH into the head node"
  type        = string
  default     = "0.0.0.0/0" # Restrict this to your IP in production
}

###############################################################################
# Locals
###############################################################################

locals {
  name_prefix = "${var.project}-${var.environment}"
  common_tags = {
    Project     = var.project
    Environment = var.environment
    ManagedBy   = "Terraform"
  }
}

###############################################################################
# VPC
###############################################################################

resource "aws_vpc" "main" {
  cidr_block           = "10.0.0.0/16"
  enable_dns_support   = true
  enable_dns_hostnames = true

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-vpc" })
}

resource "aws_internet_gateway" "igw" {
  vpc_id = aws_vpc.main.id
  tags   = merge(local.common_tags, { Name = "${local.name_prefix}-igw" })
}

###############################################################################
# Subnets
###############################################################################

# Public subnet — head node lives here (needs public IP for SSH + Ray dashboard)
resource "aws_subnet" "public" {
  vpc_id                  = aws_vpc.main.id
  cidr_block              = "10.0.1.0/24"
  availability_zone       = "${var.aws_region}a"
  map_public_ip_on_launch = true

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-public-subnet" })
}

# Private subnet — worker nodes (no public IP, egress via NAT)
resource "aws_subnet" "private" {
  vpc_id            = aws_vpc.main.id
  cidr_block        = "10.0.2.0/24"
  availability_zone = "${var.aws_region}a"

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-private-subnet" })
}

###############################################################################
# NAT Gateway (workers need outbound internet for pip installs / S3 sync)
###############################################################################

resource "aws_eip" "nat" {
  domain = "vpc"
  tags   = merge(local.common_tags, { Name = "${local.name_prefix}-nat-eip" })
}

resource "aws_nat_gateway" "nat" {
  allocation_id = aws_eip.nat.id
  subnet_id     = aws_subnet.public.id

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-nat-gw" })
  depends_on = [aws_internet_gateway.igw]
}

###############################################################################
# Route Tables
###############################################################################

resource "aws_route_table" "public" {
  vpc_id = aws_vpc.main.id

  route {
    cidr_block = "0.0.0.0/0"
    gateway_id = aws_internet_gateway.igw.id
  }

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-public-rt" })
}

resource "aws_route_table_association" "public" {
  subnet_id      = aws_subnet.public.id
  route_table_id = aws_route_table.public.id
}

resource "aws_route_table" "private" {
  vpc_id = aws_vpc.main.id

  route {
    cidr_block     = "0.0.0.0/0"
    nat_gateway_id = aws_nat_gateway.nat.id
  }

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-private-rt" })
}

resource "aws_route_table_association" "private" {
  subnet_id      = aws_subnet.private.id
  route_table_id = aws_route_table.private.id
}

###############################################################################
# Security Groups
###############################################################################

# Head node — reachable from the internet (SSH) and from workers (Ray ports)
resource "aws_security_group" "head" {
  name        = "${local.name_prefix}-head-sg"
  description = "Ray head node: SSH + Ray ports"
  vpc_id      = aws_vpc.main.id

  # SSH
  ingress {
    description = "SSH"
    from_port   = 22
    to_port     = 22
    protocol    = "tcp"
    cidr_blocks = [var.allowed_ssh_cidr]
  }

  # Redis (Ray object store / GCS)
  ingress {
    description = "Redis / Ray object store"
    from_port   = 6379
    to_port     = 6379
    protocol    = "tcp"
    cidr_blocks = ["10.0.0.0/16"] # VPC-internal only
  }

  # Ray worker communication ports
  ingress {
    description = "Ray worker ports"
    from_port   = 10001
    to_port     = 10010
    protocol    = "tcp"
    cidr_blocks = ["10.0.0.0/16"]
  }

  # Ray dashboard
  ingress {
    description = "Ray dashboard"
    from_port   = 8265
    to_port     = 8265
    protocol    = "tcp"
    cidr_blocks = [var.allowed_ssh_cidr]
  }

  # All outbound
  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-head-sg" })
}

# Worker nodes — only reachable from head node
resource "aws_security_group" "worker" {
  name        = "${local.name_prefix}-worker-sg"
  description = "Ray worker nodes: head-node traffic only"
  vpc_id      = aws_vpc.main.id

  # All traffic from head node SG
  ingress {
    description     = "All from head node"
    from_port       = 0
    to_port         = 0
    protocol        = "-1"
    security_groups = [aws_security_group.head.id]
  }

  # Inter-worker traffic (RLlib rollout synchronisation)
  ingress {
    description = "Inter-worker Ray ports"
    from_port   = 10001
    to_port     = 10010
    protocol    = "tcp"
    self        = true
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-worker-sg" })
}

###############################################################################
# S3 — Training Logs & Checkpoints
###############################################################################

resource "random_id" "bucket_suffix" {
  byte_length = 4
}

resource "aws_s3_bucket" "training" {
  bucket        = "${local.name_prefix}-training-${random_id.bucket_suffix.hex}"
  force_destroy = false # Prevent accidental deletion of checkpoints

  tags = merge(local.common_tags, { Name = "${local.name_prefix}-training-bucket" })
}

resource "aws_s3_bucket_versioning" "training" {
  bucket = aws_s3_bucket.training.id
  versioning_configuration {
    status = "Enabled"
  }
}

resource "aws_s3_bucket_server_side_encryption_configuration" "training" {
  bucket = aws_s3_bucket.training.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

resource "aws_s3_bucket_public_access_block" "training" {
  bucket                  = aws_s3_bucket.training.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

# Lifecycle rule: auto-expire intermediate checkpoints older than 30 days
resource "aws_s3_bucket_lifecycle_configuration" "training" {
  bucket = aws_s3_bucket.training.id

  rule {
    id     = "expire-old-checkpoints"
    status = "Enabled"

    filter {
      prefix = "checkpoints/"
    }

    expiration {
      days = 30
    }
  }
}

###############################################################################
# IAM — EC2 Instance Role (S3 access, no static credentials needed)
###############################################################################

resource "aws_iam_role" "ray_node" {
  name = "${local.name_prefix}-ray-node-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "ec2.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })

  tags = local.common_tags
}

resource "aws_iam_role_policy" "ray_s3" {
  name = "${local.name_prefix}-ray-s3-policy"
  role = aws_iam_role.ray_node.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "s3:GetObject",
          "s3:PutObject",
          "s3:DeleteObject",
          "s3:ListBucket",
          "s3:GetBucketLocation"
        ]
        Resource = [
          aws_s3_bucket.training.arn,
          "${aws_s3_bucket.training.arn}/*"
        ]
      }
    ]
  })
}

resource "aws_iam_role_policy" "ray_ecr" {
  name = "${local.name_prefix}-ray-ecr-policy"
  role = aws_iam_role.ray_node.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "ecr:GetAuthorizationToken",
          "ecr:BatchCheckLayerAvailability",
          "ecr:GetDownloadUrlForLayer",
          "ecr:BatchGetImage"
        ]
        Resource = "*"
      }
    ]
  })
}

resource "aws_iam_instance_profile" "ray_node" {
  name = "${local.name_prefix}-ray-node-profile"
  role = aws_iam_role.ray_node.name
}

###############################################################################
# SSH Key Pair
###############################################################################

resource "aws_key_pair" "ray" {
  key_name   = "${local.name_prefix}-key"
  public_key = file(var.ssh_public_key_path)
  tags       = local.common_tags
}

###############################################################################
# Outputs
###############################################################################

output "vpc_id" {
  value = aws_vpc.main.id
}

output "public_subnet_id" {
  value = aws_subnet.public.id
}

output "private_subnet_id" {
  value = aws_subnet.private.id
}

output "head_sg_id" {
  value = aws_security_group.head.id
}

output "worker_sg_id" {
  value = aws_security_group.worker.id
}

output "s3_bucket_name" {
  value = aws_s3_bucket.training.bucket
}

output "s3_bucket_arn" {
  value = aws_s3_bucket.training.arn
}

output "iam_instance_profile_name" {
  value = aws_iam_instance_profile.ray_node.name
}

output "key_pair_name" {
  value = aws_key_pair.ray.key_name
}
